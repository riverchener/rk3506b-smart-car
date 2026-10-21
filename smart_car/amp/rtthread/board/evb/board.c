/*
 * Board support for the RK3506 "BUS M0" (AMP co-processor of the A7 Linux).
 *
 * Everything here runs on the M0 only.  The M0 boots from the FIT image that
 * U-Boot drops into the 32 KiB window (`load = <0xfff84000>`), so this file
 * has to set up, in this order:
 *
 *   1. the vendor HAL (NVIC + the 24 MHz SYS_TIMER = TIMER5 microsecond base)
 *   2. the INTMUX, which is how the M0 receives external interrupts
 *      (the rpmsg mailbox IRQ is registered later by rpmsg_platform_rt.c)
 *   3. UART4 @1500000 on GPIO1_C2/C3 - the console the board manual calls
 *      "M0 串口", and the only place distance/temperature/humidity are printed
 *   4. the RT-Thread tick: SysTick, external reference (22.5 MHz measured,
 *      not the 24 MHz the vendor code assumes - see SC_SYSTICK_REF_HZ_MEASURED)
 *      divided down to 8 kHz = 125 us
 *   5. the RT-Thread heap (everything between .bss and the MSP stack)
 */

#include <rtthread.h>
#include <rthw.h>

#include "hal_base.h"
#include "hal_bsp.h"
#include "board.h"

/* ------------------------------------------------------------------------ */
/* Interrupt entry points                                                     */
/*                                                                            */
/* Normally these live in bsp/rockchip/common/drivers/interrupt.c, but that   */
/* file also pulls in the whole Rockchip common driver set.  This BSP builds  */
/* the kernel + vendor HAL only, so the four hooks RT-Thread needs are here.  */
/*                                                                            */
/* Cortex-M0 has no VTOR: the vector table is always at address 0 and         */
/* HAL_NVIC_SetIRQHandler() patches the entry in RAM.  That is exactly why    */
/* start_rk3506_mcu.S reserves 64 zeroed IRQ slots after the 16 core vectors. */
/* ------------------------------------------------------------------------ */
void rt_hw_interrupt_init(void)
{
    /* The vendor bare-metal M0 firmware never reprogrammed NVIC priorities
     * either; it only brought the INTMUX up.  Keep that (NUM_INTERRUPTS has
     * two different values in soc.h depending on AP/MCU flavour, so looping
     * over it would be a foot-gun on a 32-IRQ Cortex-M0 NVIC). */
#ifdef HAL_INTMUX_MODULE_ENABLED
    HAL_INTMUX_Init();
#endif
}

void rt_hw_interrupt_mask(int vector)
{
#ifdef HAL_INTMUX_MODULE_ENABLED
    if (vector >= NUM_INTERRUPTS)
    {
        HAL_INTMUX_DisableIRQ(vector);
        return;
    }
#endif
    HAL_NVIC_DisableIRQ(vector);
}

void rt_hw_interrupt_umask(int vector)
{
#ifdef HAL_INTMUX_MODULE_ENABLED
    if (vector >= NUM_INTERRUPTS)
    {
        HAL_INTMUX_EnableIRQ(vector);
        return;
    }
#endif
    HAL_NVIC_EnableIRQ(vector);
}

rt_isr_handler_t rt_hw_interrupt_install(int vector,
                                         rt_isr_handler_t handler,
                                         void *param,
                                         const char *name)
{
    RT_UNUSED(name);

#ifdef HAL_INTMUX_MODULE_ENABLED
    if (vector >= NUM_INTERRUPTS)
    {
        HAL_INTMUX_SetIRQHandler(vector, (HAL_INTMUX_HANDLER)handler, param);
        return handler;
    }
#endif
    /* vector == SysTick_IRQn (-1) lands on the SysTick slot of the vector
     * table, which is how the tick below is hooked. */
    HAL_NVIC_SetIRQHandler(vector, (NVIC_IRQHandler)handler);

    return handler;
}

/* ------------------------------------------------------------------------ */
/* Console: UART4, 1500000 8N1, RM_IO27/28 (= A27/A28 on the base board)      */
/* ------------------------------------------------------------------------ */
static void sc_console_hw_init(void)
{
    struct HAL_UART_CONFIG hal_uart_config =
    {
        .baudRate = UART_BR_1500000,
        .dataBit  = UART_DATA_8B,
        .stopBit  = UART_ONE_STOPBIT,
        .parity   = UART_PARITY_DISABLE,
    };

    HAL_PINCTRL_SetRMIO(GPIO_BANK1, GPIO_PIN_C2, RMIO_UART4_TX);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, GPIO_PIN_C3, RMIO_UART4_RX);
    HAL_UART_Init(&g_uart4Dev, &hal_uart_config);
}

void rt_hw_console_output(const char *str)
{
    while (*str != '\0')
    {
        if (*str == '\n')
        {
            HAL_UART_SerialOutChar(UART4, '\r');
        }
        HAL_UART_SerialOutChar(UART4, *str++);
    }
}

/* ------------------------------------------------------------------------ */
/* Tick                                                                       */
/* ------------------------------------------------------------------------ */
/*
 * SysTick reference clock - MEASURED, not nominal.
 *
 * The vendor BSP feeds SysTick from "the external reference" and then assumes
 * that reference is the 24 MHz oscillator (hal_systick.c: HAL_SYSTICK_Init()
 * does `rate = PLL_INPUT_OSC_RATE` after a successful CLKSRC_EXT switch, and
 * board.c did the same).  That assumption is wrong on this board by exactly
 * 16/15:
 *
 *   [实测] two independent M0 tick periods, both measured from the A7 side,
 *          came out 16/15 too long - the 1500 ms announce retry was 1.600000 s
 *          (57 samples, sigma ~2e-5) and the 100 ms SR04 push was ~106.7 ms.
 *   [代码可读] the reload actually programmed was 24e6/8000 = 3000 counts
 *          (HAL_SYSTICK_Config() writes LOAD = ticks-1, hal_systick.c:101-111),
 *          so 7500 Hz of real tick means the counter is fed 22.5 MHz, i.e.
 *          24 MHz * 15/16.
 *
 * 24 MHz / 16 * 15 is written out instead of a bare 22500000 so the number
 * keeps pointing at its origin, and the reload is derived, never hardcoded.
 * 22.5e6 / 8000 = 2812.5 counts is not an integer; 2812 gives
 * 22.5e6 / 2812 = 8001.4 Hz (+0.018 %, i.e. 100 ms -> 99.98 ms) and 2813 gives
 * 7998.6 Hz (-0.018 %); either is 3 orders of magnitude better than the 6.67 %
 * error being fixed.
 *
 * Whether the counter really is clocked at 15/16 of the nominal rate or loses
 * one interrupt in sixteen is not distinguishable from the A7 side, and BOTH
 * explanations are corrected by the same 15/16 reload scaling, so scaling the
 * reload is the fix that holds either way.  Do not restore 24e6/8000 = 3000:
 * that is the 7500 Hz tick that stretched every tick-based interval by 6.67 %.
 *
 * Keeping RT_TICK_PER_SECOND at 8000 (125 us) is deliberate - sc_carctl.c
 * needs the 125 us grid for the 1 kHz software PWM and the 2 kHz/25 % beeper
 * carrier; only the reload is corrected.
 */
#define SC_SYSTICK_REF_HZ_MEASURED  (PLL_INPUT_OSC_RATE / 16U * 15U)

static void sc_tick_isr(int vector, void *param)
{
    RT_UNUSED(vector);
    RT_UNUSED(param);

    rt_interrupt_enter();
    rt_tick_increase();
    rt_interrupt_leave();

    /* HAL_IncTick() is deliberately NOT called: with an 8 kHz tick the HAL's
     * millisecond counter would run 8x fast, and it is not needed anyway -
     * HAL_GetTick()/HAL_DelayUs() are driven by SYS_TIMER (TIMER5, 24 MHz). */
}

static void rt_idle_wfi(void)
{
    rt_ubase_t level;

    asm volatile("MRS     %0, PRIMASK\n"
                 "CPSID   I"
                 : "=r"(level) : : "memory", "cc");

    asm volatile("dsb");
    asm volatile("wfi");

    asm volatile("MSR     PRIMASK, %0"
                 : : "r"(level) : "memory", "cc");
}

/* ------------------------------------------------------------------------ */
/* Board init                                                                 */
/* ------------------------------------------------------------------------ */
int rt_hw_board_init(void)
{
    uint32_t tick_rate = SystemCoreClock;

    /* vendor HAL: NVIC, 24 MHz SYS_TIMER (TIMER5), pin controller */
    HAL_Init();
    BSP_Init();

    /* external interrupts are demultiplexed by the INTMUX on this core */
    rt_hw_interrupt_init();

    /* M0 console (UART4) - must be up before the first rt_kprintf */
    sc_console_hw_init();

    /* 8 kHz tick off SysTick.  Preferred source is the external reference (the
     * same clock family the microsecond time base uses) - but its MEASURED rate
     * is 22.5 MHz, not the 24 MHz the vendor code assumes; if this part does
     * not wire the reference up at all, fall back to the core clock. */
    if (HAL_SYSTICK_CLKSourceConfig(HAL_SYSTICK_CLKSRC_EXT) == HAL_OK)
    {
        tick_rate = SC_SYSTICK_REF_HZ_MEASURED;
    }
    HAL_SYSTICK_Config(tick_rate / RT_TICK_PER_SECOND);
    rt_hw_interrupt_install(SysTick_IRQn, sc_tick_isr, RT_NULL, "tick");
    rt_hw_interrupt_umask(SysTick_IRQn);
    HAL_SYSTICK_Enable();

    /* heap: everything the image leaves between .bss and the MSP stack */
    rt_system_heap_init(HEAP_BEGIN, HEAP_END);

    rt_thread_idle_sethook(rt_idle_wfi);

    return 0;
}

/*
 * NOTE: entry() is NOT defined here.  rtos/src/components.c already provides
 *
 *     int entry(void) { rtthread_startup(); return 0; }
 *
 * for the GCC + RT_USING_USER_MAIN combination, and Reset_Handler
 * (start_rk3506_mcu.S) calls exactly that symbol after SystemInit().
 */
