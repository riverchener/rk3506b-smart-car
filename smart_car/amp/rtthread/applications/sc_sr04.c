/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_sr04.c - HC-SR04 / RCWL-1601 ultrasonic ranging on the M0.
 *
 * Behaviour is unchanged from the bare-metal firmware: the M0 triggers the
 * sensor itself every 100 ms, caches the newest echo width and pushes it to
 * whichever A7 endpoint subscribed.  The measurement is a microsecond poll of
 * the ECHO pin against SYS_TIMER, exactly as before; RT-Thread only changes
 * *who* runs it (a thread instead of the idle loop) and at what priority.
 */

#include "sc_app.h"

#define SC_PERIODIC_SR04_TIMEOUT_US 15000U  /* keep a stuck echo line bounded */

static rt_uint32_t sc_sr04_period = 100U;   /* ms, 0 = off (no subscriber) */
static rt_uint32_t sc_sr04_echo_us;
static rt_uint32_t sc_sr04_mm;
static rt_uint32_t sc_sr04_ok;
static rt_uint32_t sc_sr04_bad;             /* consecutive implausible reads */

/* ------------------------------------------------------------------------ */
/* measurement                                                               */
/* ------------------------------------------------------------------------ */
rt_uint32_t sc_sr04_read(rt_uint32_t timeout_us, rt_uint32_t *status)
{
    rt_uint32_t t0;
    rt_uint32_t us;

    *status = SC_ST_OK;

    /*
     * Wait until ECHO is idle (low) before triggering.  A read that timed out
     * can leave it high; the "wait for the rising edge" loop below would then
     * return immediately and the next measurement would time the *leftover*
     * pulse, reporting a bogus few-hundred-microsecond distance.
     */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_HIGH)
    {
        if ((sc_now_us() - t0) > 30000U)
        {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    /* >= 10 us trigger pulse */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_HIGH);
    HAL_DelayUs(12U);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_LOW);

    /* wait for echo to rise */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_LOW)
    {
        if ((sc_now_us() - t0) > timeout_us)
        {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    /* measure the high width */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_HIGH)
    {
        if ((sc_now_us() - t0) > timeout_us)
        {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    us = sc_now_us() - t0;

    /*
     * Below ~150 us (< 2.5 cm) nothing physical can echo: that is the trigger
     * pulse coupling into a floating / unpowered echo line.  Report a failed
     * measurement instead of a fake "obstacle 2 cm ahead".
     */
    if (us < 150U)
    {
        *status = SC_ST_TIMEOUT;
        return us;
    }

    return us;
}

/* read the SR04 once and cache it */
void sc_sample_sr04(void)
{
    rt_uint32_t st = SC_ST_OK;
    rt_uint32_t us = sc_sr04_read(SC_PERIODIC_SR04_TIMEOUT_US, &st);

    if (st == SC_ST_OK)
    {
        sc_sr04_echo_us = us;
        sc_sr04_mm = (us * 10U) / 58U;
        sc_sr04_ok = 1U;
        sc_sr04_bad = 0U;
    }
    else
    {
        sc_sr04_ok = 0U;
        sc_sr04_bad++;
        if (us != 0U && (sc_sr04_bad <= 3U || (sc_sr04_bad % 20U) == 0U))
        {
            rt_kprintf("[sc] big echo=%uus! ", us);
        }
    }
}

rt_uint32_t sc_sr04_cache(rt_uint32_t *echo_us, rt_uint32_t *mm, rt_uint32_t *valid)
{
    if (echo_us != RT_NULL)
    {
        *echo_us = sc_sr04_echo_us;
    }
    if (mm != RT_NULL)
    {
        *mm = sc_sr04_mm;
    }
    if (valid != RT_NULL)
    {
        *valid = sc_sr04_ok;
    }

    return sc_sr04_ok;
}

void sc_sr04_subscribe(rt_uint32_t period_ms)
{
    sc_sr04_period = period_ms;
}

void sc_sr04_clear(void)
{
    sc_sr04_ok = 0U;
    sc_sr04_bad = 0U;
}

rt_uint32_t sc_sr04_period_ms(void)
{
    return sc_sr04_period;
}

/* ------------------------------------------------------------------------ */
/* the 100 ms ranging thread                                                 */
/* ------------------------------------------------------------------------ */
#define SC_SR04_THREAD_STACK 512
#define SC_SR04_THREAD_PRIO  3

static struct rt_thread sc_sr04_tcb;
static rt_uint8_t sc_sr04_stack[SC_SR04_THREAD_STACK];

static void sc_sr04_entry(void *parameter)
{
    rt_tick_t last = rt_tick_get();

    RT_UNUSED(parameter);

    for (;;)
    {
        rt_uint32_t period = sc_sr04_period;

        if (period == 0U)
        {
            rt_thread_mdelay(50);
            last = rt_tick_get();
            continue;
        }

        rt_thread_delay_until(&last, rt_tick_from_millisecond((rt_int32_t)period));

        sc_sample_sr04();
        sc_log_sample();        /* 10 Hz console line, subscriber or not */
        sc_push_sample();       /* rpmsg push to whoever subscribed */
    }
}

void sc_sr04_start(void)
{
    rt_thread_init(&sc_sr04_tcb, "sr04", sc_sr04_entry, RT_NULL,
                   sc_sr04_stack, sizeof(sc_sr04_stack),
                   SC_SR04_THREAD_PRIO, 10);
    rt_thread_startup(&sc_sr04_tcb);
}
