import os

# ---------------------------------------------------------------------------
# RT-Thread BSP: Rockchip RK3506 "BUS M0" (AMP co-processor of the A7 Linux)
#
# Build:  scons            (see build_m0_rtt.sh for the full install+pack flow)
# Output: rtthread.elf / rtthread.bin
#
# The M0 has exactly 32 KiB of RAM for *everything* (text + rodata + data +
# bss + heap + stack), so the toolchain flags below are size-first:
#   -Os, -ffunction-sections/-fdata-sections + --gc-sections, nano.specs,
#   no unwind tables (the M0 firmware throws no C++ exceptions).
# ---------------------------------------------------------------------------

ARCH = 'arm'
CPU = 'cortex-m0'
CROSS_TOOL = 'gcc'

if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')

PLATFORM = 'gcc'
# <SDK>/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin
# BSP lives at <SDK>/rtos/bsp/rockchip/rk3506-mcu  ->  four levels up is <SDK>.
EXEC_PATH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          '../../../../prebuilts/gcc/linux-x86/arm/'
                                          'gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin'))

if os.getenv('RTT_EXEC_PATH'):
    EXEC_PATH = os.getenv('RTT_EXEC_PATH')

BUILD = 'release'
TARGET_NAME = 'rtthread.bin'

if PLATFORM == 'gcc':
    PREFIX = 'arm-none-eabi-'
    CC = PREFIX + 'gcc'
    AS = PREFIX + 'gcc'
    AR = PREFIX + 'ar'
    LINK = PREFIX + 'gcc'
    TARGET_EXT = 'elf'
    SIZE = PREFIX + 'size'
    OBJDUMP = PREFIX + 'objdump'
    OBJCPY = PREFIX + 'objcopy'

    DEVICE = ' -mcpu=cortex-m0 -mthumb -ffunction-sections -fdata-sections'

    CFLAGS = DEVICE + ' -std=gnu99 -Os -g -Wall -Wno-unused-parameter -Wno-stringop-truncation'
    CFLAGS += ' -fno-unwind-tables -fno-asynchronous-unwind-tables'
    CFLAGS += ' -Dgcc -D__RT_THREAD__ -D__STARTUP_COPY_MULTIPLE -D__STARTUP_CLEAR_BSS_MULTIPLE'

    AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp -Wa,-mimplicit-it=thumb'
    AFLAGS += ' -D__ASSEMBLY__ -D__STARTUP_COPY_MULTIPLE -D__STARTUP_CLEAR_BSS_MULTIPLE'

    LINK_SCRIPT = 'gcc_link.ld.S'
    LFLAGS = DEVICE + ' -nostartfiles -Wl,--gc-sections,-Map=rtthread.map,-cref,-u,Reset_Handler'
    LFLAGS += ' --specs=nano.specs --specs=nosys.specs -lm -lgcc -lc'

    CPATH = ''
    LPATH = ''

    if BUILD == 'debug':
        CFLAGS += ' -O0 -gdwarf-2'
        AFLAGS += ' -gdwarf-2'
    else:
        CFLAGS += ' -Os'

    POST_ACTION = OBJCPY + ' -O binary -R .note -R .note.gnu.build-id -R .comment -S $TARGET ' + TARGET_NAME + '\n'
    POST_ACTION += SIZE + ' $TARGET\n'
