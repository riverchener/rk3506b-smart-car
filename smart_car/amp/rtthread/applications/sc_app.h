/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_app.h - shared definitions for the RK3506B M0 AMP service (RT-Thread port).
 *
 * The wire protocol, the pin assignment and the device semantics are copied
 * verbatim from the bare-metal firmware (amp/sc_amp.c + amp/sc_carctl.h), so
 * the A7 side (/dev/mycarctl, /dev/mysr04, /dev/mybuzzer, sc_sensors.ko,
 * sc_client) does not notice the difference.
 *
 *   struct sc_frame { u16 cmd; u16 seq; u32 status; u32 a; u32 b; u32 c; u32 t; }
 *
 *   0x0001 PING      -            -                  t = M0 timestamp [us]
 *   0x0002 TIME      -            -                  a = lo32, b = hi32
 *   0x0003 JITTER    iterations   period_us          a/b/c = min/max/avg us
 *   0x0004 BUZZER    freq_hz      duration_ms        a/b/c = periods/freq/elapsed
 *   0x0005 SR04      timeout_us   -                  a = echo_us, b = distance_mm
 *   0x0006 DHT11     -            -                  status = NODEV (A7 owns it)
 *   0x0007 LOOP      pulses       -                  a = pulses_ok, b = last_us
 *   0x0008 INFO      -            -                  a = fw ver, b = heap bytes,
 *                                                     c = timer counts per us
 *   0x000A SUBSCRIBE a = sr04 period ms, b = dht11 period ms (0 = off)
 *   0x000B LATEST    -            -                  cached a = mm, b = T*10,
 *                                                     c = H*10, status = valid bits
 *   0x000C TONE      freq_hz (0 = stop), non blocking  a = freq
 *   0x000D PUSH_DHT  a = temp*10, b = hum*10         a/b echoed back
 *   0x000E CARCTL    a = mask (bit0 L, bit1 R, bit2 hazard, bit3 reverse,
 *                    bit4 low beam, bit5 high beam)   a = applied mask
 *
 *   response cmd = request | 0x8000, status: 0 ok, 1 timeout, 2 no device,
 *   3 checksum, 4 bad arg
 *
 *   unsolicited push: 0x8081 SC_EVT_SAMPLE
 *     status = valid bits, a = echo us, b = distance mm,
 *     c = (temp*10 << 16) | hum*10, t = timestamp
 */

#ifndef SC_APP_H
#define SC_APP_H

#include <rtthread.h>
#include "hal_base.h"

/* ------------------------------------------------------------------------ */
/* pins (GPIO1) - identical to the bare-metal firmware                       */
/* ------------------------------------------------------------------------ */
#define SC_PIN_TRIG    GPIO_PIN_C6      /* SR04 TRIG,  gpio 54 = A23        */
#define SC_PIN_ECHO    GPIO_PIN_C7      /* SR04 ECHO,  gpio 55 = A24        */
#define SC_PIN_BUZZER  GPIO_PIN_D0      /* buzzer, active low, gpio 56      */
#define SC_PIN_LED_L   GPIO_PIN_C0      /* left  indicator, gpio 48 = A29   */
#define SC_PIN_LED_R   GPIO_PIN_C1      /* right indicator, gpio 49 = A30   */
#define SC_PIN_DHT     GPIO_PIN_C4      /* DHT11 - owned by the A7, the M0
                                         * must not drive it (see docs)     */

/* ------------------------------------------------------------------------ */
/* rpmsg service identity                                                    */
/* ------------------------------------------------------------------------ */
#define SC_MASTER_ID   0U
#define SC_REMOTE_ID   3U               /* bus M0 */
#define SC_EPT_ID      0x4004U
#define SC_EPT_NAME    "rpmsg-smartcar"
#define SC_FW_VERSION  0x00010000U      /* 1.0.0 - unchanged on purpose */

/* ------------------------------------------------------------------------ */
/* protocol                                                                  */
/* ------------------------------------------------------------------------ */
#define SC_RSP          0x8000U

#define SC_CMD_PING      0x0001U
#define SC_CMD_TIME      0x0002U
#define SC_CMD_JITTER    0x0003U
#define SC_CMD_BUZZER    0x0004U
#define SC_CMD_SR04      0x0005U
#define SC_CMD_DHT11     0x0006U
#define SC_CMD_LOOP      0x0007U
#define SC_CMD_INFO      0x0008U
#define SC_CMD_SUBSCRIBE 0x000AU
#define SC_CMD_LATEST    0x000BU
#define SC_CMD_TONE      0x000CU
#define SC_CMD_PUSH_DHT  0x000DU
#define SC_CMD_CARCTL    0x000EU

#define SC_EVT_SAMPLE     0x8081U
#define SC_EVT_SR04_VALID 0x01U
#define SC_EVT_DHT_VALID  0x02U

#define SC_ST_OK        0U
#define SC_ST_TIMEOUT   1U
#define SC_ST_NODEV     2U
#define SC_ST_CHECKSUM  3U
#define SC_ST_BADARG    4U

struct sc_frame
{
    rt_uint16_t cmd;
    rt_uint16_t seq;
    rt_uint32_t status;
    rt_uint32_t a;
    rt_uint32_t b;
    rt_uint32_t c;
    rt_uint32_t t;
};

/* linker script symbols (gcc_link.ld.S) */
extern uint32_t __heap_start[];
extern uint32_t __heap_end[];
extern uint32_t __linux_share_rpmsg_start__[];

/* ------------------------------------------------------------------------ */
/* liveness probe                                                            */
/*                                                                            */
/* Pinned at an absolute DDR address (0x03e00000 + 0x639c) that sits inside   */
/* the device tree's `amp@3e00000` no-map reservation, so the A7 can read it  */
/* with devmem and the Linux allocator can never hand that memory to anyone:  */
/*                                                                            */
/*     devmem 0x03e0639c   -> 0x414D5042   ("AMPB") magic                     */
/*     devmem 0x03e063a0   -> step (6 = service fully up)                     */
/*     devmem 0x03e063a4   -> served request counter                          */
/*                                                                            */
/* Same three addresses as the bare-metal firmware, so the existing board     */
/* scripts keep working.  The M0 can reach DDR directly - it already does for  */
/* the rpmsg vrings at 0x03c00000.                                            */
/* ------------------------------------------------------------------------ */
struct sc_probe_block
{
    volatile rt_uint32_t magic;
    volatile rt_uint32_t step;
    volatile rt_uint32_t extra;
};

#define SC_PROBE_BASE   ((struct sc_probe_block *)0x03E0639CUL)
#define SC_PROBE_MAGIC  0x414D5042U

#define SC_PROBE(n)      do { SC_PROBE_BASE->step = (n); } while (0)
#define SC_PROBE_FAIL(n) do { SC_PROBE_BASE->step = (0xE0U | (n)); } while (0)

/* ------------------------------------------------------------------------ */
/* sc_hw.c - time base, pins, console                                        */
/* ------------------------------------------------------------------------ */
/* 24 counts/us: SYS_TIMER (TIMER5) is clocked from PLL_INPUT_OSC_RATE.
 * Deliberately a compile-time constant - dividing a 64 bit counter by a
 * runtime value drags the 64 bit division helpers into the image. */
#define SC_CNT_PER_US  24U

rt_uint32_t sc_now_us(void);
void sc_hw_init(void);
void sc_log_sample(void);

/* ------------------------------------------------------------------------ */
/* sc_sr04.c - HC-SR04 / RCWL-1601 ranging                                   */
/* ------------------------------------------------------------------------ */
rt_uint32_t sc_sr04_read(rt_uint32_t timeout_us, rt_uint32_t *status);
void sc_sample_sr04(void);
void sc_sr04_start(void);
void sc_sr04_subscribe(rt_uint32_t period_ms);
void sc_sr04_clear(void);
rt_uint32_t sc_sr04_period_ms(void);
/* cached result accessors (used by the protocol handler and the log line) */
rt_uint32_t sc_sr04_cache(rt_uint32_t *echo_us, rt_uint32_t *mm, rt_uint32_t *valid);

/* ------------------------------------------------------------------------ */
/* sc_carctl.c - indicator LEDs (1 kHz software PWM) + reverse beeper        */
/* ------------------------------------------------------------------------ */
void sc_carctl_start(void);
void sc_carctl_mask_set(rt_uint8_t mask);
rt_uint8_t sc_carctl_mask_get(void);
void sc_carctl_tone(rt_uint32_t freq_hz);
rt_uint32_t sc_carctl_tone_freq(void);

/* ------------------------------------------------------------------------ */
/* sc_rpmsg.c - rpmsg transport, protocol dispatch, sample pushes            */
/* ------------------------------------------------------------------------ */
void sc_rpmsg_start(void);
void sc_push_sample(void);
void sc_dht_push(rt_uint32_t temp_x10, rt_uint32_t hum_x10);
rt_uint32_t sc_dht_cache(rt_uint32_t *temp_x10, rt_uint32_t *hum_x10);
void sc_dht_subscribe(rt_uint32_t period_ms);

#endif /* SC_APP_H */
