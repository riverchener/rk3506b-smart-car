/*
 * sc_carctl.h —— 车机本地 LED / 倒车蜂鸣（M0）
 * ============================================================================
 * 为什么单独抽成一个头文件：
 *   这段"转向灯 1.5 Hz 闪 / 近光 50% 亮度 / 远光 100% / 倒车 60-120-620 ms 半音量蜂鸣"
 *   的时序是纯逻辑，**可以在电脑上跑测试**（amp/test_carctl_host.c）。
 *   抽出来之后，固件和测试用的是**同一份代码**，不会出现"测试测的是另一份实现"。
 *
 * 使用约定（包含者必须先提供）：
 *   · `static uint32_t sc_tone_freq;`  —— 手动报警音的频率（0 = 没在响），互斥用
 *   · 宏 SC_LED_SET(pin, level) / SC_BZ_SET(level)：真正写引脚的地方，
 *     固件里映射到 HAL_GPIO_SetPinLevel，测试里映射到"记录波形"
 *   · 引脚名 SC_PIN_LED_L / SC_PIN_LED_R / SC_PIN_BUZZER，电平 GPIO_HIGH / GPIO_LOW
 *
 * 掩码位：bit0 左转 bit1 右转 bit2 双闪 bit3 倒车 bit4 近光 bit5 远光
 */
#ifndef SC_CARCTL_H
#define SC_CARCTL_H

#include <stdint.h>

/* 固件里默认映射到 HAL；测试可以在包含前自行 #define 覆盖 */
#ifndef SC_LED_SET
#define SC_LED_SET(pin, level) HAL_GPIO_SetPinLevel(GPIO1, pin, level)
#endif
#ifndef SC_BZ_SET
#define SC_BZ_SET(level)       HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER, level)
#endif

static uint8_t  sc_car_mask;
static uint8_t  sc_led_on_l;
static uint8_t  sc_led_on_r;
static uint8_t  sc_led_duty;            /* 0..100 */
static uint8_t  sc_pwm_phase;           /* 0 = 周期高电平段，1 = 低电平段 */
static uint32_t sc_led_due_us;          /* 下一次 PWM 翻转 */
static uint32_t sc_blink_due_us;        /* 1.5 Hz 闪烁相位翻转 */
static uint8_t  sc_blink_on;
static uint8_t  sc_rev_step;            /* 倒车节拍状态机 */
static uint32_t sc_rev_due_us;
static uint8_t  sc_rev_gate;            /* 1 = 这一段该响 */
static uint8_t  sc_bz_level;
static uint32_t sc_bz_due_us;

#define SC_LED_PWM_PERIOD_US 1000U      /* 1 kHz */
#define SC_BLINK_HALF_US     333000U    /* 1.5 Hz → 每 333 ms 翻转 */
#define SC_REV_BEEP_US       60000U     /* 滴 60 ms */
#define SC_REV_GAP_US        120000U    /* 停 120 ms */
#define SC_REV_PAUSE_US      620000U    /* 一组之间的长停 620 ms */
#define SC_REV_FREQ_HZ       2000U      /* 倒车提示音频率 */

/* 车机本地掩码变化时调用：复位时序状态（由 SC_CMD_CARCTL 处理里调用） */
static void sc_carctl_reset(uint32_t now)
{
    sc_blink_due_us = now;
    sc_led_due_us = now;
    sc_pwm_phase = 0U;
    sc_rev_step = 0U;
    sc_rev_due_us = now;
    sc_bz_level = 0U;
    sc_bz_due_us = now;
    if (!(sc_car_mask & 0x08U)) {                 /* 不倒了：停蜂鸣 */
        sc_rev_gate = 0U;
        if (sc_tone_freq == 0U)
            SC_BZ_SET(GPIO_HIGH);
    }
}

/*
 * 车机本地 LED / 倒车蜂鸣的服务（每个 idle 迭代调一次，非阻塞）
 * ----------------------------------------------------------------------------
 *   近光 → 两 LED 常亮 50% 亮度（1 kHz 软件 PWM）
 *   远光 → 两 LED 常亮 100%
 *   转向/双闪 → 在当前亮度档上 1.5 Hz 闪烁；没开灯时转向用满亮度
 *   倒车 → "滴滴-滴滴-滴滴"（60/120/620 ms 节拍），占空比 25%
 *          ≈ 音量是手动报警（50% 方波）的一半，与手动 TONE 互斥
 */
static void sc_service_carctl(uint32_t now)
{
    /* --- 1.5 Hz 闪烁相位 --- */
    if ((int32_t)(now - sc_blink_due_us) >= 0) {
        sc_blink_on ^= 1U;
        sc_blink_due_us = now + SC_BLINK_HALF_US;
    }

    const uint8_t hazard = (sc_car_mask & 0x04U) ? 1U : 0U;
    const uint8_t turn_l = ((sc_car_mask & 0x01U) || hazard) ? 1U : 0U;
    const uint8_t turn_r = ((sc_car_mask & 0x02U) || hazard) ? 1U : 0U;
    const uint8_t beam   = (sc_car_mask & 0x30U) ? 1U : 0U;   /* 近光或远光亮着 */
    const uint8_t hi     = (sc_car_mask & 0x20U) ? 1U : 0U;

    sc_led_on_l = turn_l ? sc_blink_on : (beam ? 1U : 0U);
    sc_led_on_r = turn_r ? sc_blink_on : (beam ? 1U : 0U);
    sc_led_duty = hi ? 100U : ((beam ? 50U : 100U));

    /* --- 1 kHz 软件 PWM：一个周期两个沿（先高电平段，再低电平段） --- */
    if ((int32_t)(now - sc_led_due_us) >= 0) {
        const uint32_t on_us = (SC_LED_PWM_PERIOD_US * sc_led_duty) / 100U;
        if (sc_pwm_phase == 0U) {
            SC_LED_SET(SC_PIN_LED_L, sc_led_on_l ? GPIO_HIGH : GPIO_LOW);
            SC_LED_SET(SC_PIN_LED_R, sc_led_on_r ? GPIO_HIGH : GPIO_LOW);
            sc_led_due_us = now + (on_us > 0U ? on_us : 1U);
            sc_pwm_phase = 1U;
        } else {
            SC_LED_SET(SC_PIN_LED_L, GPIO_LOW);
            SC_LED_SET(SC_PIN_LED_R, GPIO_LOW);
            sc_led_due_us = now + ((SC_LED_PWM_PERIOD_US - on_us) > 0U
                                   ? (SC_LED_PWM_PERIOD_US - on_us) : 1U);
            sc_pwm_phase = 0U;
        }
    }

    /* --- 倒车蜂鸣 --- */
    if (sc_car_mask & 0x08U) {
        if ((int32_t)(now - sc_rev_due_us) >= 0) {
            switch (sc_rev_step) {
            case 0U: sc_rev_gate = 1U; sc_rev_due_us = now + SC_REV_BEEP_US;  sc_rev_step = 1U; break;
            case 1U: sc_rev_gate = 0U; sc_rev_due_us = now + SC_REV_GAP_US;   sc_rev_step = 2U; break;
            case 2U: sc_rev_gate = 1U; sc_rev_due_us = now + SC_REV_BEEP_US;  sc_rev_step = 3U; break;
            default: sc_rev_gate = 0U; sc_rev_due_us = now + SC_REV_PAUSE_US; sc_rev_step = 0U; break;
            }
        }
        if (sc_tone_freq == 0U && sc_rev_gate) {          /* 与手动报警互斥 */
            if ((int32_t)(now - sc_bz_due_us) >= 0) {
                const uint32_t period = 1000000U / SC_REV_FREQ_HZ;
                if (sc_bz_level == 0U) {
                    SC_BZ_SET(GPIO_LOW);                    /* active low = 响 */
                    sc_bz_due_us = now + (period / 4U > 0U ? period / 4U : 1U);
                } else {
                    SC_BZ_SET(GPIO_HIGH);
                    sc_bz_due_us = now + (period * 3U) / 4U;
                }
                sc_bz_level ^= 1U;
            }
        }
    } else if (sc_rev_gate) {
        sc_rev_gate = 0U;
        sc_rev_step = 0U;
        if (sc_tone_freq == 0U)
            SC_BZ_SET(GPIO_HIGH);                           /* 停响 */
    }
}

#endif /* SC_CARCTL_H */
