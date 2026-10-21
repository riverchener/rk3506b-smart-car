/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * M0 runtime shims for the RK3506B AMP service.
 *
 * The M0 has 32 KiB of RAM in total and *code lives in it too* (the image is
 * loaded to DDR at 0x03e00000 and the M0 sees it as address 0x0), so every byte
 * of library code directly steals from the rpmsg-lite heap.  These two shims
 * remove roughly 5 KiB of library code/data that this firmware never uses:
 *
 * 1) malloc()/free()
 *    rpmsg-lite's bare-metal environment calls malloc() for its instance and
 *    its endpoints.  Linking newlib's allocator pulls in _malloc_r/_free_r
 *    (~1.9 KiB of text), the 1 KiB `__malloc_av_` arena, the 1 KiB reent
 *    structure and the lock objects.  The service allocates a handful of
 *    blocks once at boot and never frees, so a bump allocator over the linker
 *    script's heap region is both smaller and faster.
 *
 * 2) __aeabi_unwind_cpp_pr0/1/2
 *    The vendor startup assembly carries .fnstart/.fnend directives, whose
 *    .ARM.exidx entries reference the ARM EHABI personality routine.  That one
 *    reference pulls in the complete C++ unwinder (libunwind.o, unwind-arm.o,
 *    pr-support.o, ~2.4 KiB).  There are no exceptions in this firmware, so
 *    the personality routine is never invoked and a stub is enough.
 */
#include <stddef.h>
#include <stdint.h>

/* --- 1. bump allocator over the linker script's heap ---------------------- */
extern uint32_t __heap_start[];
extern uint32_t __heap_end[];

static uint32_t sc_heap_used_bytes;

static inline uint32_t sc_heap_base(void)
{
    return (uint32_t)(size_t)__heap_start;
}

uint32_t sc_heap_total(void)
{
    return (uint32_t)((size_t)__heap_end - (size_t)__heap_start);
}

uint32_t sc_heap_used(void)
{
    return sc_heap_used_bytes;
}

void *malloc(size_t size)
{
    uint32_t p = sc_heap_base() + sc_heap_used_bytes;

    size = (size + 7U) & ~(size_t)7U;           /* keep 8 byte alignment */
    if ((p + (uint32_t)size) > (uint32_t)(size_t)__heap_end) {
        return NULL;
    }
    sc_heap_used_bytes += (uint32_t)size;

    return (void *)(size_t)p;
}

void free(void *ptr)
{
    (void)ptr;                                  /* never freed in this firmware */
}

/* --- 2. ARM EHABI personality routine stubs ------------------------------- */
void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}

void __aeabi_unwind_cpp_pr2(void)
{
}
