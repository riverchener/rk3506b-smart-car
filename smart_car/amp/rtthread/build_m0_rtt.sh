#!/bin/bash
# ============================================================================
# Build + pack the RT-Thread M0 firmware for the RK3506B AMP smart car.
#
#   ./build_m0_rtt.sh [output_dir]
#
# What it does, in order:
#   1. mirror this directory into the SDK as rtos/bsp/rockchip/rk3506-mcu/
#      (the SDK's own build scripts look for BSPs there) and copy the *shared*
#      sc_carctl.h from ../ so the timing state machine cannot drift
#   2. scons   -> rtthread.elf / rtthread.bin
#   3. report text/data/bss/heap and the 32 KiB budget
#   4. wrap rtthread.bin into a FIT image (amp.img) with the vendor mkimage,
#      exactly the way device/rockchip/common/scripts/mk-amp.sh does
#
# Nothing outside the BSP directory is modified: the vendor bare-metal project
# (hal/project/rk3506-mcu) and the Linux kernel are left completely alone.
# ============================================================================
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
AMP=$(cd "$HERE/.." && pwd)
SDK=${RK_SDK_DIR:-$HOME/atk_dlrk3506_linux6.1_release_v1.3.1_20260326}
BSP=$SDK/rtos/bsp/rockchip/rk3506-mcu
MKIMAGE=$SDK/rtos/bsp/rockchip/tools/mkimage
TC=$SDK/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi-
OUT=${1:-$HERE/out}

[ -d "$SDK/rtos" ] || { echo "SDK not found: $SDK (set RK_SDK_DIR)"; exit 1; }
mkdir -p "$OUT"

echo "== 1/4 install BSP -> $BSP"
# always from a clean tree: a stale build/ or .sconsign.dblite keeps an old
# object order from a previous configuration alive, which changes the link
# layout by a few bytes of alignment padding
rm -rf "$BSP"
mkdir -p "$BSP"
cp -a "$HERE/board" "$HERE/applications" "$HERE/Image" "$BSP/"
cp -a "$HERE/rtconfig.py" "$HERE/rtconfig.h" "$HERE/Kconfig" "$HERE/.config" \
      "$HERE/SConstruct" "$HERE/SConscript" "$HERE/gcc_link.ld.S" "$HERE/hal_conf.h" "$BSP/"
# shared with the bare-metal firmware and amp/test_carctl_host.c
cp -a "$AMP/sc_carctl.h" "$BSP/applications/sc_carctl.h"

# ---------------------------------------------------------------------------
# Guard against the one regression that is invisible from the outside.
#
# The A7 side (kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c, rk_rpmsg_notify)
# puts ONLY the link id in the mailbox message (tx_msg->cmd = link_id & 0xFF)
# and, after the single master-init handshake, only ever notifies for an ODD
# queue id (its send queue).  In rpmsg_lite_remote_init() the remote's vrings are
# swapped (vq_names[0]="tx_vq", vq_names[1]="rx_vq"; tvq=vqs[0], rvq=vqs[1]), so:
#
#   handshake (first notify)  -> vq0 = RL_GET_VQ_ID(link_id,0) -> tx_callback
#                                -> link_state=1, the M0 may announce
#   every later notify        -> vq1 = RL_GET_VQ_ID(link_id,1) -> rx_callback
#                                -> the ONLY code that drains rvq and calls
#                                   sc_rx_cb()
#
# If the second line is missing (a "vq0 is the receive queue" misreading of
# rpmsg-lite), the M0 comes up and announces normally, but never consumes a
# single A7 frame: vring1 used.idx stays 0, the A7's 64 send descriptors are
# never returned and /dev/mysr04 answers -EIO while dmesg fills with
# "rpmsg_trysendto(0x4004) failed: -12".  Fail the build instead.
# ---------------------------------------------------------------------------
if ! grep -q 'env_isr(RL_GET_VQ_ID(link_id, 1))' "$BSP/applications/rpmsg_platform_rt.c"; then
    echo "FATAL: applications/rpmsg_platform_rt.c never notifies the remote rx_vq"
    echo "       (no env_isr(RL_GET_VQ_ID(link_id, 1))).  The M0 would link up and"
    echo "       announce, but never drain the A7->M0 vring => ENOMEM on the A7."
    exit 1
fi

echo "== 2/4 scons"
cd "$BSP"
export RTT_ROOT=$SDK/rtos
export BSP_ROOT=$BSP
# RT-Thread's building.py does `src = list(set(src))` for every group, and a
# Python set iterates in hash order - which is randomised per process unless
# PYTHONHASHSEED is fixed.  Without this the object files reach the linker in a
# different order on every run, the HAL/application symbols land at different
# addresses and even the image size can shift by a few bytes of alignment pad,
# so the md5 was not reproducible.  Pin it.
export PYTHONHASHSEED=0
: > "$OUT/.build_start"          # mtime marker, see the cleanup below
# ---------------------------------------------------------------------------
# `set -e` cannot see the exit status of the left-hand side of a pipeline, so
# with a bare `scons ... | tee ...` a failed compile/link still leaves this
# script exiting 0 and the PREVIOUS build's rtthread.elf/amp.img sitting in
# $OUT - which is how a "fixed" source tree kept shipping an old image for
# days.  Capture PIPESTATUS[0] (bash) and check it *after* the shared-tree
# cleanup below, so a failed build still tidies $SDK/hal.
# ---------------------------------------------------------------------------
scons -j"$(nproc)" 2>&1 | tee "$OUT/scons.log"
SCONS_RC=${PIPESTATUS[0]}

# ---------------------------------------------------------------------------
# RT-Thread's SCons can only re-root object files that live *under* the
# SConscript's own directory into a variant dir.  The vendor HAL and rpmsg-lite
# live in the shared ../common/hal tree instead, so their .o files land next to
# the sources.  Put the shared tree back exactly the way we found it: the
# bare-metal project builds the very same sources with different flags (its
# hal_debug.h does not route HAL_DBG through rt_kprintf), so a stale foreign .o
# there is a real trap - and a stale differently-flagged start_rk3506_mcu.o
# would even change the linked image.
#
# Selected by mtime marker rather than by parsing the log, because the log line
# for an assembler object is not always "CC".
# ---------------------------------------------------------------------------
removed=$(find "$SDK/hal" -name '*.o' -newer "$OUT/.build_start" | wc -l)
find "$SDK/hal" -name '*.o' -newer "$OUT/.build_start" -delete
echo "    shared-tree objects removed again: $removed"

if [ "$SCONS_RC" -ne 0 ]; then
    echo "FATAL: scons exited $SCONS_RC (see $OUT/scons.log)."
    echo "       $OUT still holds the PREVIOUS build - do not package it."
    exit 1
fi
if [ ! rtthread.elf -nt "$OUT/.build_start" ]; then
    echo "FATAL: rtthread.elf was not regenerated by this build."
    exit 1
fi

echo "== 3/4 memory budget (32 KiB = 32768 B total for everything)"
"$TC"size rtthread.elf

"$TC"nm rtthread.elf > "$OUT/rtthread.nm"
python3 - "$OUT/rtthread.nm" <<'PYEOF'
import re, sys

syms = {}
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 3:
        try:
            syms[p[2]] = int(p[0], 16)
        except ValueError:
            pass

def sym(name):
    v = syms.get(name)
    return v if v is not None else 0

heap_start, heap_end = sym('__heap_start'), sym('__heap_end')
stack_top, stack_limit = sym('__StackTop'), sym('__StackLimit')
bss_start, bss_end = sym('__bss_start__'), sym('__bss_end__')

heap = heap_end - heap_start
if stack_top and stack_limit:
    stack = stack_top - stack_limit
else:
    stack = 0x400                                   # __STACK_SIZE in the .ld.S
image = bss_end                                     # text+rodata+data+bss
budget = 0x8000

print('  .text/.rodata/.data/.bss  : 0x%08x .. 0x%08x' % (0, bss_end))
print('  .bss                      : 0x%08x .. 0x%08x (%d B)' %
      (bss_start, bss_end, bss_end - bss_start))
print('  heap (linker ASSERT >=1.5K): %6d B' % heap)
print('  MSP stack (exceptions/boot): %6d B' % stack)
print('  ------------------------------------------------------------')
print('  image  (text+data+bss)    : %6d B' % image)
print('  heap + stack              : %6d B' % (heap + stack))
print('  TOTAL                     : %6d B  of %d  -> %d B spare' %
      (image + heap + stack, budget, budget - (image + heap + stack)))
assert image + heap + stack <= budget, 'M0 RAM region overflow'
PYEOF

echo "== 4/4 pack amp.img (FIT, load 0xfff84000)"
PK=$(mktemp -d)
cp rtthread.bin "$PK/mcu.bin"
cp "$HERE/Image/amp_mcu_rtt.its" "$PK/amp.its"
( cd "$PK" && "$MKIMAGE" -f amp.its -E -p 0xe00 amp.img >/dev/null )
cp "$PK/amp.img" "$OUT/amp.img"
cp rtthread.bin "$OUT/mcu.bin"
cp rtthread.elf "$OUT/rtthread.elf"
cp rtthread.map "$OUT/rtthread.map" 2>/dev/null || true
rm -rf "$PK"

python3 - "$OUT/mcu.bin" "$OUT/amp.img" <<'PYEOF'
import hashlib, sys
bin_ = open(sys.argv[1], 'rb').read()
img  = open(sys.argv[2], 'rb').read()
print('  mcu.bin   %7d B  md5 %s' % (len(bin_), hashlib.md5(bin_).hexdigest()))
print('  amp.img   %7d B  md5 %s' % (len(img), hashlib.md5(img).hexdigest()))
print('  payload embedded in FIT: %s (offset %s)' %
      (bin_ in img, img.find(bin_) if bin_ in img else '-'))
assert bin_ in img, 'FIT image does not contain the firmware payload!'
PYEOF

echo
echo "artefacts in $OUT :"
ls -l "$OUT"
