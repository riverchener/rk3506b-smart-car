/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * HAL configuration for the RK3506 "BUS M0" running RT-Thread.
 *
 * This is the same HAL the vendor bare-metal M0 project uses
 * (hal/project/rk3506-mcu/src/hal_conf.h), with three deliberate changes:
 *
 *   1. HAL_DBG_USING_RTT_SERIAL - HAL_DBG()/HAL_DBG_ERR() are routed to
 *      rt_kprintf() instead of the HAL's own vsnprintf wrapper, so the HAL
 *      keeps its diagnostics at no extra code cost on a 32 KiB part.
 *   2. HAL_TSADC_MODULE_ENABLED removed - the M0 service never touches the
 *      thermal ADC.
 *   3. HAL_GPIO_IRQ_DISPATCH_FEATURE_ENABLED / HAL_GPIO_VIRTUAL_MODEL_*
 *      removed - only plain pin level/direction GPIO is used here.
 *
 * SYS_TIMER (TIMER5, 24 MHz) is the microsecond time base: the HAL's
 * HAL_DelayUs()/HAL_GetSysTimerCount() and this firmware's 125 us tick all
 * run off the same PLL_INPUT_OSC_RATE reference.
 */

#ifndef _HAL_CONF_H_
#define _HAL_CONF_H_

/* CPU config */
#define SOC_RK3506
#define HAL_MCU_CORE
#define HAL_BUS_MCU_CORE

#define SYS_TIMER TIMER5 /* System timer designation (RK TIMER) */

/* HAL Driver Config */
#define HAL_CRU_MODULE_ENABLED
#define HAL_INTMUX_MODULE_ENABLED
#define HAL_IRQ_HANDLER_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_MBOX_MODULE_ENABLED
#define HAL_NVIC_MODULE_ENABLED
#define HAL_PINCTRL_MODULE_ENABLED
#define HAL_SYSTICK_MODULE_ENABLED
#define HAL_TIMER_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* HAL_DBG SUB CONFIG: send HAL diagnostics to the RT-Thread console (UART4) */
#define HAL_DBG_ON
#ifdef HAL_DBG_ON
#define HAL_DBG_USING_RTT_SERIAL
#define HAL_DBG_INFO_ON
#define HAL_DBG_WRN_ON
#define HAL_DBG_ERR_ON
#endif

#endif
