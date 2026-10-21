/*
 * Board support header for the RK3506 BUS-M0 RT-Thread BSP.
 *
 * The M0 has no flash: the whole image (text/rodata/data/bss/heap/stack) lives
 * in the 32 KiB window U-Boot maps at M0 address 0.  The heap therefore starts
 * right after .bss and ends where the 1 KiB MSP stack begins - both symbols come
 * from gcc_link.ld.S.
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>

extern uint32_t __heap_start[];
extern uint32_t __heap_end[];

#define HEAP_BEGIN  ((void *)__heap_start)
#define HEAP_END    ((void *)__heap_end)

#endif /* __BOARD_H__ */
