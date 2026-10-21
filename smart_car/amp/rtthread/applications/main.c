/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - RT-Thread entry for the RK3506B M0 AMP service.
 *
 * Boot chain (unchanged from the bare-metal firmware):
 *
 *   BootROM -> SPL -> U-Boot (CPU0) -> FIT image "mcu" -> mcu.bin
 *     -> sip_smc_mcu_config(MCU_CODE_START_ADDR = 0xfff84000, entry)
 *     -> the M0 maps that 32 KiB window at its address 0
 *     -> Reset_Handler (start_rk3506_mcu.S) -> SystemInit() -> entry()
 *     -> rtthread_startup() -> rt_hw_board_init() -> "main" thread -> main()
 *
 * Cold boot only: there is no remoteproc and no way to restart the M0 from
 * Linux - if the M0 dies, the board has to be power cycled (this was already
 * true of the bare-metal firmware).
 */

#include "sc_app.h"

int main(void)
{
    /*
     * A7-visible liveness marker, written as the very first thing: the probe
     * block lives in the DTS `amp@3e00000` reserved region, so Linux can read
     * it with devmem even when no UART is attached to the M0.
     */
    SC_PROBE_BASE->magic = SC_PROBE_MAGIC;
    SC_PROBE_BASE->extra = 0U;
    SC_PROBE(0);

    /* microsecond time base (SYS_TIMER/TIMER5) + SR04/buzzer/LED pins */
    sc_hw_init();
    SC_PROBE(1);

    rt_kprintf("\n[sc] RK3506B M0 on RT-Thread %d.%d.%d (tick %d Hz)\n",
               (RT_VERSION >> 16) & 0xFF, (RT_VERSION >> 8) & 0xFF, RT_VERSION & 0xFF,
               RT_TICK_PER_SECOND);
    {
        rt_size_t total = 0U, used = 0U, max_used = 0U;

        rt_memory_info(&total, &used, &max_used);
        rt_kprintf("[sc] heap %uB total, %uB used; sr04 C6/C7, buzzer D0(lo), ledL C0, ledR C1\n",
                   (unsigned)total, (unsigned)used);
    }

    /* the carctl (125 us) and ranging (100 ms) threads start immediately: the
     * M0 keeps measuring and serving its own sensors whether or not the A7 has
     * subscribed, exactly as before */
    sc_carctl_start();
    sc_sr04_start();
    SC_PROBE(2);

    /* the rpmsg thread brings the transport up and then blocks until the A7
     * talks to the mailbox, so it must not run here in the main thread */
    sc_rpmsg_start();

    /* main() returns: the RT-Thread main thread exits and its stack goes back
     * to the heap.  The four worker threads keep running. */
    return 0;
}
