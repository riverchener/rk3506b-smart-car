/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_libc.c - two tiny shims that keep several KiB of library code out of a
 *             32 KiB part.  Same idea as amp/sc_runtime.c in the bare-metal
 *             firmware, adapted to RT-Thread.
 *
 * 1) __aeabi_unwind_cpp_pr0/1/2
 *
 *    start_rk3506_mcu.S carries .fnstart/.fnend directives, so its .ARM.exidx
 *    entries reference the ARM EHABI personality routine.  That single
 *    reference pulls in the complete C++ unwinder (unwind-arm.o, libunwind.o,
 *    pr-support.o, bpabi.o, plus abort/raise/signal from newlib).  Measured on
 *    this exact build: 2.7 KiB of the M0's 32 KiB.  Nothing here throws a C++
 *    exception - the vendor bare-metal M0 project stubs these out too - so
 *    empty stubs are correct, not just cheap.
 *
 * 2) printf
 *
 *    rpmsg-lite's virtqueue.c calls env_print() from virtqueue_dump(), and
 *    env_print == printf.  --gc-sections does drop virtqueue_dump(), but the
 *    newlib stdio chain is pulled in during archive resolution anyway.  Our own
 *    printf on top of RT-Thread's rt_vsnprintf keeps newlib's stdio (and its
 *    1 KiB malloc arena + reent + file locks) out of the image, while still
 *    printing whatever rpmsg-lite wants to say, on the M0 console.
 */

#include <rtthread.h>

#if defined(RT_USING_CONSOLE)
#include <stdarg.h>
#endif

void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}

void __aeabi_unwind_cpp_pr2(void)
{
}

#if defined(RT_USING_CONSOLE)
int printf(const char *fmt, ...)
{
    char buf[RT_CONSOLEBUF_SIZE];
    va_list ap;

    va_start(ap, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    rt_kprintf("%s", buf);

    return 0;
}
#else
int printf(const char *fmt, ...)
{
    RT_UNUSED(fmt);

    return 0;
}
#endif
