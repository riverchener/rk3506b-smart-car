/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_hw.c - hardware glue for the RK3506B M0 AMP service (RT-Thread port):
 *           the microsecond time base, the pin ownership table and the 10 Hz
 *           console line.
 *
 * Pin ownership is the contract with the A7 side and has not changed:
 *
 *   GPIO1_C2/C3  UART4 @1500000   M0 console        (RM_IO 27/28 = A27/A28)
 *   GPIO1_C4     DHT11            **the A7 owns it, the M0 never touches it**
 *   GPIO1_C6     SR04 TRIG        M0
 *   GPIO1_C7     SR04 ECHO        M0
 *   GPIO1_D0     buzzer (lo)      M0
 *   GPIO1_C0/C1  indicator L / R  M0
 */

#include "sc_app.h"

/* ------------------------------------------------------------------------ */
/* microsecond time base                                                     */
/* ------------------------------------------------------------------------ */
/*
 * SYS_TIMER = TIMER5, free running at PLL_INPUT_OSC_RATE = 24 MHz.
 *
 * The 64 bit counter is truncated to 32 bits *before* the division: one wrap
 * is 178 s, every interval this firmware measures is under 1 s, and unsigned
 * subtraction of two truncated values still gives the right difference.  This
 * avoids pulling __aeabi_uldivmod (~200 B) into a 32 KiB part.
 */
rt_uint32_t sc_now_us(void)
{
    return (rt_uint32_t)HAL_GetSysTimerCount() / SC_CNT_PER_US;
}

/* ------------------------------------------------------------------------ */
/* pins                                                                      */
/* ------------------------------------------------------------------------ */
void sc_hw_init(void)
{
    /* Take the SR04 / buzzer / indicator pads out of the RMIO peripheral
     * matrix.  RM_IO pads must be released with RMIO_NO_FUNCTION (function 7
     * + matrix 0); HAL_PINCTRL_SetIOMUX(..., PIN_CONFIG_MUX_FUNC0) is wrong
     * on this SoC and was a real bring-up trap - see amp/sc_amp.c. */
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_TRIG, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_ECHO, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_BUZZER, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_LED_L, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_LED_R, RMIO_NO_FUNCTION);

    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_TRIG, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_BUZZER, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_ECHO, GPIO_IN);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_LED_L, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_LED_R, GPIO_OUT);

    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_LOW);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER, GPIO_HIGH);   /* active low = silent */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_LED_L, GPIO_LOW);     /* LED: high = on */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_LED_R, GPIO_LOW);

    /* GPIO1_C4 (DHT11) is deliberately NOT configured: the A7 bit-bangs it. */
}

/* ------------------------------------------------------------------------ */
/* 10 Hz console line                                                        */
/* ------------------------------------------------------------------------ */
/*
 * Labels stay plain ASCII English on purpose (the bare-metal firmware learned
 * this the hard way): XCOM decodes GBK while MobaXterm/PuTTY/picocom default
 * to UTF-8, so a Chinese label can only ever be right on one of them.
 *
 *   [sc] distance=1177mm  temperature=28.4C  humidity=74.0%
 *
 * distance is measured here; temperature/humidity are what the A7 read off
 * the DHT11 and pushed over SC_CMD_PUSH_DHT (10 s grace period).
 */
void sc_log_sample(void)
{
    rt_uint32_t echo = 0U, mm = 0U, sr04_valid = 0U;
    rt_uint32_t temp_x10 = 0U, hum_x10 = 0U;
    rt_uint32_t dht_valid;

    sc_sr04_cache(&echo, &mm, &sr04_valid);
    RT_UNUSED(echo);
    dht_valid = sc_dht_cache(&temp_x10, &hum_x10);

    /* printed piecewise so the invalid-value shapes ("----", "--.-C",
     * "--.-%") come out byte-identical to the bare-metal firmware */
    rt_kprintf("[sc] distance=");
    if (sr04_valid)
    {
        rt_kprintf("%umm", mm);
    }
    else
    {
        rt_kprintf("----");
    }

    rt_kprintf("  temperature=");
    if (dht_valid)
    {
        rt_kprintf("%u.%uC", temp_x10 / 10U, temp_x10 % 10U);
    }
    else
    {
        rt_kprintf("--.-C");
    }

    rt_kprintf("  humidity=");
    if (dht_valid)
    {
        rt_kprintf("%u.%u%%", hum_x10 / 10U, hum_x10 % 10U);
    }
    else
    {
        rt_kprintf("--.-%");
    }

    rt_kprintf("\n");
}
