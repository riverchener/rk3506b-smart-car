/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_carctl.c - the M0's four real-time duties that are *not* sensor related:
 *
 *   1. 1 kHz software PWM on the two indicator LEDs
 *        low beam  -> both LEDs at ~50 % brightness
 *        high beam -> both LEDs at 100 %
 *   2. 1.5 Hz turn-signal / hazard flashing (333 ms phase)
 *   3. reverse beeper "beep-beep-beep", 60/120/620 ms rhythm with a 2 kHz
 *      carrier at 25 % duty (= half the volume of the manual alarm), mutually
 *      exclusive with a manual SC_CMD_TONE
 *   4. the non-blocking manual TONE square wave itself
 *
 * The state machine itself is NOT re-implemented here: it is the same
 * sc_carctl.h the bare-metal firmware and the host-side timing test
 * (amp/test_carctl_host.c) share, so the timing cannot drift between the two.
 *
 * What RT-Thread changes is only the driving: the bare-metal firmware called
 * sc_service_carctl() from its idle loop.  Here it runs in a dedicated thread
 * that wakes on every tick.  RT_TICK_PER_SECOND is 8000 (125 us) precisely
 * because every interval in sc_carctl.h is an exact multiple of 125 us
 * (1000, 500, 333000, 60000, 120000, 620000, 125/375), so the tick grid
 * quantises none of the edges.
 */

#include "sc_app.h"

/*
 * The whole design of this file rests on one number being an exact multiple of
 * the tick: every interval in sc_carctl.h is a multiple of 125 us, and the tick
 * has to be at least as fine as 125 us or the 2 kHz / 25 % beeper carrier
 * cannot be produced at all.  Fail the build instead of shipping a firmware
 * whose indicator and beeper timing is silently wrong.
 *
 * (The vendor's rtos/src/Kconfig declares `range 10 1000` for
 * RT_TICK_PER_SECOND.  That range is a menuconfig UI constraint of this SDK
 * fork - the SCons build reads rtconfig.h, which is authoritative.  Do not run
 * `scons --useconfig` / menuconfig on this BSP: it would clamp the tick.)
 */
#if (RT_TICK_PER_SECOND < 8000)
#error "RT_TICK_PER_SECOND must be >= 8000 (125 us) for the 1 kHz software PWM and the 2 kHz/25% beeper carrier"
#endif

/* sc_carctl.h expects this to exist before it is included: the manual alarm
 * frequency, used for the "reverse beeper vs manual tone" interlock. */
static rt_uint32_t sc_tone_freq;

#include "sc_carctl.h"

static rt_uint32_t sc_tone_due_us;
static rt_uint32_t sc_tone_level;

/* ------------------------------------------------------------------------ */
/* mask + tone control (called from the rpmsg thread / ISR context)          */
/* ------------------------------------------------------------------------ */
void sc_carctl_mask_set(rt_uint8_t mask)
{
    sc_car_mask = (rt_uint8_t)(mask & 0x3FU);
    sc_carctl_reset(sc_now_us());
}

rt_uint8_t sc_carctl_mask_get(void)
{
    return sc_car_mask;
}

void sc_carctl_tone(rt_uint32_t freq_hz)
{
    sc_tone_freq = freq_hz;
    if (sc_tone_freq == 0U)
    {
        sc_tone_level = 0U;
        HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER, GPIO_HIGH);   /* active low = off */
    }
    else
    {
        sc_tone_due_us = sc_now_us();
    }
}

rt_uint32_t sc_carctl_tone_freq(void)
{
    return sc_tone_freq;
}

/* ------------------------------------------------------------------------ */
/* service: the shared state machine + the manual tone square wave           */
/* ------------------------------------------------------------------------ */
static void sc_carctl_service(rt_uint32_t now)
{
    /* indicators + reverse beeper rhythm: the shared implementation */
    sc_service_carctl(now);

    /* non-blocking manual alarm tone (square wave, toggled from here) */
    if (sc_tone_freq != 0U && (rt_int32_t)(now - sc_tone_due_us) >= 0)
    {
        rt_uint32_t half = 500000U / sc_tone_freq;

        if (half == 0U)
        {
            half = 1U;
        }
        sc_tone_level ^= 1U;
        HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER,
                             sc_tone_level ? GPIO_LOW : GPIO_HIGH);
        sc_tone_due_us = now + half;
    }
}

/* ------------------------------------------------------------------------ */
/* the 125 us service thread                                                 */
/* ------------------------------------------------------------------------ */
#define SC_CARCTL_THREAD_STACK 384
#define SC_CARCTL_THREAD_PRIO  2       /* highest of the three workers */

static struct rt_thread sc_carctl_tcb;
static rt_uint8_t sc_carctl_stack[SC_CARCTL_THREAD_STACK];

static void sc_carctl_entry(void *parameter)
{
    RT_UNUSED(parameter);

    for (;;)
    {
        /* one tick = 125 us at RT_TICK_PER_SECOND = 8000 */
        rt_thread_delay(1);
        sc_carctl_service(sc_now_us());
    }
}

void sc_carctl_start(void)
{
    /* LEDs start dark, beeper silent */
    sc_carctl_tone(0U);
    sc_carctl_mask_set(0U);

    rt_thread_init(&sc_carctl_tcb, "carctl", sc_carctl_entry, RT_NULL,
                   sc_carctl_stack, sizeof(sc_carctl_stack),
                   SC_CARCTL_THREAD_PRIO, 10);
    rt_thread_startup(&sc_carctl_tcb);
}
