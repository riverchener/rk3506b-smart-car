/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * RK3506B AMP heterogeneous service - Cortex-M0 side.
 *
 * The M0 owns the microsecond-level peripherals and serves them over RPMsg:
 *   - SR04 ultrasonic : TRIG/ECHO pulse timing (GPIO1_C6 / GPIO1_C7)
 *   - DHT11 is NOT here: it moved to the A7, see sc_sensors.ko
 *   - buzzer          : precise square wave (GPIO1_D0, active low)
 *   - microsecond timebase + its own scheduling-jitter statistics
 *
 * Protocol: fixed 24-byte frames, one request -> one response.
 *
 *   struct sc_frame { u16 cmd; u16 seq; u32 status; u32 a; u32 b; u32 c; u32 t; }
 *
 *   request cmd   a                b                  response
 *   -----------   ---------------  -----------------  ----------------------------------
 *   0x0001 PING   -                -                  t = M0 timestamp [us]
 *   0x0002 TIME   -                -                  a = lo32, b = hi32, t = timestamp
 *   0x0003 JITTER iterations       period_us          a = min_us, b = max_us, c = avg_us
 *   0x0004 BUZZER freq_hz          duration_ms        a = periods, b = freq, c = elapsed_us
 *   0x0005 SR04   timeout_us       -                  a = echo_us, b = distance_mm
 *   0x0006 DHT11  -                -                  status = NODEV (the A7 owns it)
 *   0x0007 LOOP   pulses           -                  a = pulses_ok, b = last_us
 *   0x0008 INFO   -                -                  a = fw version, b = heap bytes,
 *                                                     c = timer counts per us
 *   status: 0 = ok, 1 = timeout, 2 = no device, 3 = checksum, 4 = bad arg
 */
#include "hal_base.h"
#include "rpmsg_lite.h"
#include "rpmsg_ns.h"

#include <string.h>

#define SC_MASTER_ID   0U
#define SC_REMOTE_ID   3U               /* bus M0 */

#define SC_EPT_ID      0x4004U
#define SC_EPT_NAME    "rpmsg-smartcar"

#define SC_FW_VERSION  0x00010000U      /* 1.0.0 */

/* --- pins (GPIO1) --------------------------------------------------------- */
#define SC_PIN_DHT     GPIO_PIN_C4      /* GPIO1_C4 = gpio 52, DHT11 data   */
#define SC_PIN_TRIG    GPIO_PIN_C6      /* GPIO1_C6 = gpio 54, SR04 TRIG    */
#define SC_PIN_ECHO    GPIO_PIN_C7      /* GPIO1_C7 = gpio 55, SR04 ECHO    */
#define SC_PIN_BUZZER  GPIO_PIN_D0      /* GPIO1_D0 = gpio 56, buzzer (lo)  */
/* 转向灯 LED（用户外接）：高电平点亮，GPIO -> LED -> 330Ω -> GND */
#define SC_PIN_LED_L   GPIO_PIN_C0      /* GPIO1_C0 = gpio 48 = A29 左转灯  */
#define SC_PIN_LED_R   GPIO_PIN_C1      /* GPIO1_C1 = gpio 49 = A30 右转灯  */

/* --- protocol ------------------------------------------------------------- */
#define SC_RSP          0x8000U

#define SC_CMD_PING     0x0001U
#define SC_CMD_TIME     0x0002U
#define SC_CMD_JITTER   0x0003U
#define SC_CMD_BUZZER   0x0004U
#define SC_CMD_SR04     0x0005U
#define SC_CMD_DHT11    0x0006U
#define SC_CMD_LOOP     0x0007U
#define SC_CMD_INFO     0x0008U
#define SC_CMD_SUBSCRIBE 0x000AU            /* a = SR04 period ms (0=off), b = DHT11 period ms */
#define SC_CMD_LATEST   0x000BU             /* instant answer from the cache */
#define SC_CMD_TONE     0x000CU             /* a = buzzer freq Hz (0 = off), non blocking */
#define SC_CMD_PUSH_DHT 0x000DU             /* A7 -> M0: a = temp*10, b = humidity*10
                                             * (the A7 owns the DHT11; this lets the M0's
                                             *  console print distance + T + H together) */
#define SC_CMD_CARCTL   0x000EU             /* A7 -> M0: a = 车机本地掩码（/dev/mycarctl）
                                             *   bit0 左转 bit1 右转 bit2 双闪 bit3 倒车
                                             *   bit4 近光 bit5 远光
                                             * 回执 a = 已生效掩码（A7 读设备可拿到） */

/* unsolicited event pushed to the subscriber */
#define SC_EVT_SAMPLE     0x8081U
#define SC_EVT_SR04_VALID 0x01U
#define SC_EVT_DHT_VALID  0x02U
/* sample frame layout: a = sr04 echo us, b = distance mm, c = temp*10<<16 | hum*10 */

#define SC_ST_OK        0U
#define SC_ST_TIMEOUT   1U
#define SC_ST_NODEV     2U
#define SC_ST_CHECKSUM  3U
#define SC_ST_BADARG    4U

struct sc_frame {
    uint16_t cmd;
    uint16_t seq;
    uint32_t status;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t t;
};

/* --- state ---------------------------------------------------------------- */
extern uint32_t __linux_share_rpmsg_start__[];
extern uint32_t __heap_start[];
extern uint32_t __heap_end[];

static struct rpmsg_lite_instance *sc_inst;
static struct rpmsg_lite_endpoint *sc_ept;
static volatile uint32_t sc_master_ept;
static volatile int sc_rx_ready;
static struct sc_frame sc_req;
static uint32_t sc_cnt_per_us = PLL_INPUT_OSC_RATE / 1000000U;

/*
 * The conversion below must use a *compile time* constant: dividing a 64-bit
 * counter by a runtime variable drags in the 64-bit division helpers (~400 B of
 * the M0's tiny 32 KiB RAM).  24 counts/us is what both the HAL (TimerDelayUs)
 * and the boot-time calibration measure.
 */
#define SC_CNT_PER_US 24U

/* --- continuous sampling state -------------------------------------------- */
/*
 * Sampling runs by itself from boot on: the M0 owns the sensors, keeps the
 * newest values cached and pushes them to whoever subscribes.  Linux never has
 * to poll for the values to stay fresh.
 *
 * The DHT11 does NOT live here: after a second attempt to sample it on the M0
 * failed (32 KiB of DDR-execution cannot resolve the 26 us bit windows, see
 * docs/模块职责_20260921.md 第 7 章) the A7 took it back and now pushes each
 * reading over SC_CMD_PUSH_DHT.  The M0 only prints what it is given.
 */
static uint32_t sc_sr04_period_ms = 100U;       /* 0 = off */
#define SC_PERIODIC_SR04_TIMEOUT_US 15000U      /* keep the idle loop responsive */
static uint32_t sc_sr04_due_us;
static uint32_t sc_sr04_echo_us;
static uint32_t sc_sr04_mm;
static uint32_t sc_sr04_ok;                     /* flag */
static uint32_t sc_sr04_bad;                    /* consecutive implausible/timeout reads */
static uint32_t sc_sample_seq;
static volatile uint32_t sc_push_count;

/*
 * DHT11 (GPIO1_C4): period 0 = off, and it stays off by default.
 *
 * The A7 owns this sensor (sc_sensors.ko bit-bangs it, docs/模块职责_20260921.md
 * 第 7 章) and pushes each reading here with SC_CMD_PUSH_DHT, so a local period
 * would only make the M0 drive the same single-wire pad at the same time as the
 * A7 - two masters on one bus.  It is enabled again only when Linux explicitly
 * subscribes with a non-zero period (dht11_local=0).
 */
static uint32_t sc_dht_period_ms = 0U;
static uint32_t sc_dht_due_us;
static uint32_t sc_dht_temp_x10;
static uint32_t sc_dht_hum_x10;
static uint32_t sc_dht_ok;                      /* last attempt succeeded */
static uint32_t sc_dht_fresh;                   /* a valid value is available (within grace) */
static uint32_t sc_dht_last_ok_us;              /* when the last good read happened */
static uint32_t sc_dht_from_a7;                 /* values were pushed by the A7 */
static uint32_t sc_dht_a7_us;                   /* when the A7 pushed them */
/*
 * The M0's bit timing is coarser than the A7 kernel driver's (the A7 measures
 * with ktime, the M0 counts loop iterations), so an occasional read fails its
 * checksum.  Never throw the value away for that: retry sooner than the normal
 * period, and keep reporting the last good value for a while - the same strategy
 * the A7 module uses (1 s cache + 30 s grace).
 */
#define SC_DHT_RETRY_US  500000U                /* retry a failed read after 500 ms */
#define SC_DHT_GRACE_US  10000000U              /* last value stays valid for 10 s */
/* failure forensics, printed to the console */
static uint32_t sc_dht_stage;                   /* 1 handshake, 2 bits */
static uint32_t sc_dht_bit;
static uint32_t sc_dht_w0;                      /* last low width  [us] */
static uint32_t sc_dht_w1;                      /* last high width [us] */
static uint32_t sc_dht_idle;                    /* bus level before the start pulse */
static uint32_t sc_dht_fail;                    /* consecutive failures */

/* non-blocking buzzer tone (driven from the idle loop) */
static uint32_t sc_tone_freq;
static uint32_t sc_tone_due_us;
static uint32_t sc_tone_level;

/*
 * 车机本地掩码（/dev/mycarctl → SC_CMD_CARCTL）与它驱动的东西
 * ----------------------------------------------------------------------------
 *   bit0 左转  bit1 右转  bit2 双闪  bit3 倒车  bit4 近光  bit5 远光
 *
 * · 近光 → 两 LED 常亮 50% 亮度（1 kHz 软件 PWM）
 * · 远光 → 两 LED 常亮 100%
 * · 转向/双闪 → 在当前亮度档上按 1.5 Hz 闪烁；没开灯时转向用满亮度
 * · 倒车 → 蜂鸣"滴滴-滴滴-滴滴"（60/120/620 ms 节拍），占空比 25%
 *          ≈ 音量是手动报警（50% 方波）的一半；与手动 TONE 互斥
 */
/*
 * 本地 LED / 倒车蜂鸣的实现已抽到 sc_carctl.h：
 * 这样固件与电脑上的时序测试（test_carctl_host.c）用**同一份代码**。
 * 这里要保证 sc_tone_freq 已经定义（手动报警互斥用），并且提供默认的引脚写宏。
 */
#include "sc_carctl.h"

/*
 * bring-up progress probe.
 *
 * These live in .bss, and the M0's .bss is in DDR (load address 0x03e00000),
 * so the A7 can read them with devmem even though the M0's SRAM is TCM-private
 * and its console may not be wired up.  Read them at
 *     0x03e00000 + <symbol address from nm>
 * magic 0x414D5042 means the M0 executed this service at all.
 */
volatile uint32_t sc_probe_magic;
volatile uint32_t sc_probe_step;
volatile uint32_t sc_probe_extra;

#define SC_PROBE(n) do { sc_probe_step = (n); } while (0)
#define SC_PROBE_FAIL(n) do { sc_probe_step = (0xE0U | (n)); } while (0)

/* --- microsecond timebase ------------------------------------------------- */
static void sc_time_init(void)
{
    uint64_t t0, t1;

    /* SYS_TIMER is documented as PLL_INPUT_OSC_RATE (24 MHz) - measure anyway */
    t0 = HAL_GetSysTimerCount();
    HAL_DelayUs(100000U);
    t1 = HAL_GetSysTimerCount();
    if (t1 > t0) {
        uint32_t c = (uint32_t)((t1 - t0) / 100000U);

        if (c != 0U) {
            sc_cnt_per_us = c;
        }
    }
}

static uint32_t sc_now_us(void)
{
    return (uint32_t)(HAL_GetSysTimerCount() / SC_CNT_PER_US);
}

/* --- real-time console ----------------------------------------------------
 *
 * Same UART4 @1500000 link HAL_DBG uses (GPIO1_C2 TX / GPIO1_C3 RX).  The M0
 * prints every sample it takes *itself*, so the serial console is the ground
 * truth: the readings below exist whether or not any Linux client subscribes.
 *
 * Temperature/humidity: the sensor is owned by the A7 (it reads it far more
 * reliably - see docs/模块职责_20260921.md 第 7 章).  The A7 sends what it read
 * with SC_CMD_PUSH_DHT and the M0 prints it here, so one console line carries
 * distance + temperature + humidity.
 *
 * Deliberately hand-rolled instead of printf(): newlib's vfprintf needs a
 * ~512 B stack and drags in the whole formatting machinery, and this M0 has
 * only 512 B of stack and 32 KiB of RAM in total.
 */
static void sc_putc(char c)
{
    if (c == '\n') {
        HAL_UART_SerialOutChar(UART4, '\r');
    }
    HAL_UART_SerialOutChar(UART4, c);
}

static void sc_puts(const char *s)
{
    while (*s != '\0') {
        sc_putc(*s++);
    }
}

static void sc_putu(uint32_t v)
{
    char b[10];
    uint32_t i = 0U;

    if (v == 0U) {
        sc_putc('0');
        return;
    }
    while (v != 0U) {
        b[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i != 0U) {
        sc_putc(b[--i]);
    }
}

/* fixed point x10 -> "26.3" */
static void sc_putx10(uint32_t x10)
{
    sc_putu(x10 / 10U);
    sc_putc('.');
    sc_putc((char)('0' + (x10 % 10U)));
}

/*
 * Is there a temperature/humidity value worth printing?
 *
 * Two possible sources: the M0's own sampling (sc_dht_fresh, which has its own
 * retry/grace logic) or the A7 pushing what it read via SC_CMD_PUSH_DHT - the
 * A7 owns the DHT11 because its bit timing is far more reliable (see
 * docs/模块职责_20260921.md 第 7 章).
 */
static uint32_t sc_dht_valid(void)
{
    if (sc_dht_from_a7 != 0U) {
        return ((sc_now_us() - sc_dht_a7_us) < SC_DHT_GRACE_US) ? 1U : 0U;
    }

    return sc_dht_fresh;
}

/*
 * Console labels: plain ASCII English on purpose.
 *
 * A first version printed Chinese labels (距离/温度/湿度).  Two things went
 * wrong with that and both are worth remembering:
 *   - the terminal in use (XCOM on Chinese Windows) decodes **GBK**, so the
 *     UTF-8 strings came out as mojibake ("璺濈"); the firmware had to carry
 *     GBK byte literals, i.e. the line only looked right in one specific
 *     terminal;
 *   - anyone using MobaXterm / PuTTY / picocom with UTF-8 then saw garbage.
 * English labels cannot be mis-decoded by either side, so they are the default.
 * (If Chinese is ever wanted: GBK 距离=BE E0 C0 EB 温度=CE C2 B6 C8
 *  湿度=CA AA B6 C8, UTF-8 = E8B79DE7A6BB / E6B8A9E5BAA6 / E6B9BFE5BAA6.)
 */
#define SC_LBL_DIST  "distance"
#define SC_LBL_TEMP  "temperature"
#define SC_LBL_HUM   "humidity"

/*
 * One console line per SR04 sample (10 Hz):
 *
 *   [sc] distance=1177mm  temperature=28.4C  humidity=74.0%
 *
 * Distance is measured by this M0; temperature/humidity are what the A7 read and
 * pushed over (SC_CMD_PUSH_DHT).
 */
static void sc_log_sample(void)
{
    uint32_t dht_ok = sc_dht_valid();

    sc_puts("[sc] ");
    sc_puts(SC_LBL_DIST);
    sc_puts("=");
    if (sc_sr04_ok) {
        sc_putu(sc_sr04_mm);
        sc_puts("mm");
    } else {
        sc_puts("----");
    }
    sc_puts("  ");
    sc_puts(SC_LBL_TEMP);
    sc_puts("=");
    if (dht_ok) {
        sc_putx10(sc_dht_temp_x10);
        sc_puts("C");
    } else {
        sc_puts("--.-C");
    }
    sc_puts("  ");
    sc_puts(SC_LBL_HUM);
    sc_puts("=");
    if (dht_ok) {
        sc_putx10(sc_dht_hum_x10);
        sc_puts("%");
    } else {
        sc_puts("--.-%");
    }
    sc_puts("\n");
}

/* --- pins ----------------------------------------------------------------- */
static void sc_pins_init(void)
{
    /* take the SR04 / buzzer pins out of the RMIO matrix -> plain M0 GPIO */
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_TRIG, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_ECHO, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_BUZZER, RMIO_NO_FUNCTION);
    /* 转向灯 LED 引脚同样从 RMIO 矩阵里拿出来当普通 GPIO（高电平点亮） */
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_LED_L, RMIO_NO_FUNCTION);
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_LED_R, RMIO_NO_FUNCTION);

    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_TRIG, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_BUZZER, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_ECHO, GPIO_IN);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_LED_L, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_LED_R, GPIO_OUT);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_LOW);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER, GPIO_HIGH);  /* active low = silent */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_LED_L, GPIO_LOW);    /* LED 高电平点亮 */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_LED_R, GPIO_LOW);

    /*
     * DHT11 data line - same treatment as the SR04 pins: RM_IO matrix -> plain
     * GPIO, plus the SoC's internal pull-up.
     *
     * DO NOT use HAL_PINCTRL_SetIOMUX(..., PIN_CONFIG_MUX_FUNC0) here.  The
     * board schematic (core board pin table) shows this pad's function list:
     *
     *   DSMC_SLV_D2 | -- | -- | -- | -- | FLEXBUS0_D7 | DSMC_D11 |
     *   VO_LCDC_D7 | GPIO1_C4_d | ...
     *
     * i.e. mux 0 is DSMC_SLV_D2, a memory-controller signal, not GPIO.  A first
     * retry used mux 0 and the pad was taken over by that peripheral: the bus
     * read back as a steady LOW, Linux could not take it back, and the DHT11 on
     * both sides looked dead.  The RMIO path (function 7 + matrix 0) is the one
     * that behaves as a GPIO - it is what the SR04 pins C6/C7 use, and what the
     * M0 used in the state where Linux read the DHT fine.
     */
    HAL_PINCTRL_SetRMIO(GPIO_BANK1, SC_PIN_DHT, RMIO_NO_FUNCTION);
    (void)HAL_PINCTRL_SetParam(GPIO_BANK1, SC_PIN_DHT, PIN_CONFIG_PUL_UP);
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_DHT, GPIO_IN);
}

/*
 * DHT11 single-wire read on the M0.
 *
 * Protocol: pull the bus low >= 18 ms, release it, the sensor answers with
 * 80 us low + 80 us high and then 40 bits.  Each bit is ~50 us low followed by
 * a high of 26-28 us (0) or 70 us (1).  5 bytes, the 5th is the checksum.
 *
 * Timing notes - this is why the first attempts failed:
 *
 *   1. The M0 executes from DDR and a HAL call costs tens of microseconds, so
 *      polling with HAL_GPIO_GetPinLevel() cannot even *see* a 26 us pulse
 *      (measured here: the same call overhead makes HAL_DelayUs(100) take
 *      ~147 us).  The loops below therefore read the GPIO input register
 *      directly: SC_DHT_HIGH() is a single load from GPIO1->EXT_PORT.
 *   2. No absolute microsecond thresholds either.  The handshake's 80 us high
 *      phase is measured with the very same loop and used as the reference:
 *      26 us -> hi*2 < ref, 70 us -> hi*2 > ref.  Self-calibrating, so the
 *      classification does not depend on how fast the loop actually runs.
 *   3. Interrupts are masked from the bus release to the last bit, because an
 *      rpmsg mailbox ISR firing mid-bit destroys the read (the A7 kernel driver
 *      protects the same sequence with local_irq_save()).
 */
#define SC_DHT_HIGH()    ((GPIO1->EXT_PORT & SC_PIN_DHT) != 0U)
#define SC_DHT_WAIT_MAX  8000U      /* loop-iteration cap == "no sensor" */

static uint32_t sc_dht11_read(uint32_t *temp_x10, uint32_t *hum_x10, uint32_t *status)
{
    uint8_t data[5] = { 0U, 0U, 0U, 0U, 0U };
    uint32_t i, j, lo, hi, n, ref = 0U, ok = 0U;

    *status = SC_ST_OK;
    *temp_x10 = 0U;
    *hum_x10 = 0U;
    sc_dht_bit = 0xFFFFFFFFU;
    sc_dht_w0 = 0U;
    sc_dht_w1 = 0U;
    sc_dht_idle = SC_DHT_HIGH() ? 1U : 0U;

    /* start signal: hold the bus low >= 18 ms, then release it */
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_DHT, GPIO_OUT);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_DHT, GPIO_LOW);
    HAL_DelayMs(20U);

    __disable_irq();
    HAL_GPIO_SetPinDirection(GPIO1, SC_PIN_DHT, GPIO_IN);

    do {
        /*
         * handshake: the sensor pulls low 80 us (i=0 waits for that low), then
         * high 80 us (i=1), then pulls low again to start the first bit (i=2).
         * The i=2 phase doubles as the timing reference for the data bits.
         */
        sc_dht_stage = 1U;
        for (i = 0U; i < 3U; i++) {
            uint32_t want = (i == 1U) ? 0U : 1U;    /* stay while level == want */
            uint32_t phase_us = (i == 0U) ? 0U : 80U;

            n = 0U;
            while ((SC_DHT_HIGH() ? 1U : 0U) == want) {
                if (++n > SC_DHT_WAIT_MAX) {
                    sc_dht_w0 = n;
                    sc_dht_bit = i;
                    *status = SC_ST_NODEV;
                    break;
                }
            }
            if (*status != SC_ST_OK) {
                break;
            }
            if (phase_us == 80U) {
                ref = n;            /* 80 us in loop counts */
            }
        }
        if (*status != SC_ST_OK) {
            break;
        }

        /* 40 data bits */
        sc_dht_stage = 2U;
        for (i = 0U; i < 40U; i++) {
            lo = 0U;
            while (!SC_DHT_HIGH()) {
                if (++lo > SC_DHT_WAIT_MAX) {
                    sc_dht_bit = i;
                    sc_dht_w0 = lo;
                    *status = SC_ST_TIMEOUT;
                    break;
                }
            }
            if (*status != SC_ST_OK) {
                break;
            }
            hi = 0U;
            while (SC_DHT_HIGH()) {
                if (++hi > SC_DHT_WAIT_MAX) {
                    sc_dht_bit = i;
                    sc_dht_w1 = hi;
                    *status = SC_ST_TIMEOUT;
                    break;
                }
            }
            if (*status != SC_ST_OK) {
                break;
            }
            sc_dht_w0 = lo;
            sc_dht_w1 = hi;
            data[i >> 3] = (uint8_t)(data[i >> 3] << 1);
            if (hi * 2U > ref) {    /* 70 us high -> 1, 26 us high -> 0 */
                data[i >> 3] |= 1U;
            }
        }
        if (*status != SC_ST_OK) {
            break;
        }

        for (j = 0U; j < 4U; j++) {
            data[4] = (uint8_t)(data[4] + data[j]);
        }
        if (data[4] != 0U) {
            *status = SC_ST_CHECKSUM;
            break;
        }

        *hum_x10 = (uint32_t)data[0] * 10U + (uint32_t)data[1];
        *temp_x10 = (uint32_t)data[2] * 10U + (uint32_t)data[3];
        ok = 1U;
    } while (0);

    __enable_irq();

    return ok;
}

/* read the DHT11 once and cache it */
static void sc_sample_dht(void)
{
    uint32_t t = 0U, h = 0U, st = SC_ST_OK;

    if (sc_dht11_read(&t, &h, &st)) {
        sc_dht_temp_x10 = t;
        sc_dht_hum_x10 = h;
        sc_dht_ok = 1U;
        sc_dht_fresh = 1U;
        sc_dht_from_a7 = 0U;            /* our own reading takes over from the A7's */
        sc_dht_last_ok_us = sc_now_us();
        if (sc_dht_fail != 0U) {
            sc_puts(" dht11 ok ");
            sc_putu(sc_dht_fail);
            sc_puts(" tries\n");
        }
        sc_dht_fail = 0U;
        sc_puts("[sc] dht11 OK T=");
        sc_putx10(t);
        sc_puts("C H=");
        sc_putx10(h);
        sc_puts("%\n");
    } else {
        sc_dht_ok = 0U;
        sc_dht_fail++;
        /*
         * Keep the last good value: it only becomes invalid once the grace
         * period since the last successful read has elapsed.
         */
        if (sc_dht_last_ok_us == 0U ||
            (sc_now_us() - sc_dht_last_ok_us) > SC_DHT_GRACE_US) {
            sc_dht_fresh = 0U;
        }
        /* first three failures, then one every five (i.e. every 10 s) */
        if (sc_dht_fail <= 3U || (sc_dht_fail % 5U) == 0U) {
            sc_puts("[sc] dht11 FAIL st=");
            sc_putu(st);
            sc_puts(" idle=");
            sc_putu(sc_dht_idle);
            sc_puts(" stage=");
            sc_putu(sc_dht_stage);
            sc_puts(" idx=");
            sc_putu(sc_dht_bit);
            sc_puts(" lowcnt=");
            sc_putu(sc_dht_w0);
            sc_puts(" hicnt=");
            sc_putu(sc_dht_w1);
            sc_puts(" level=");
            sc_putu((HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_DHT) == GPIO_HIGH) ? 1U : 0U);
            sc_puts("\n");
        }
        if (sc_dht_idle == 0U) {
            sc_puts(" busLOW\n");       /* 2026-09-29 压短：32KB TCM 放不下（详见启动横幅注释） */
        } else if (sc_dht_stage <= 1U) {
            sc_puts(" noAck\n");
        }
    }
}

/* --- SR04 ultrasonic ------------------------------------------------------ */
static uint32_t sc_sr04_read(uint32_t timeout_us, uint32_t *status)
{
    uint32_t t0;
    uint32_t us;

    *status = SC_ST_OK;

    /*
     * Wait until the echo line is idle (low) before triggering.  A read that
     * timed out can leave it high (we bail out in the middle of a pulse); the
     * "wait for the rising edge" loop below would then return immediately and
     * the next measurement would time the *leftover* pulse, reporting a bogus
     * few-hundred-microsecond distance instead of the real one.
     */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_HIGH) {
        if ((sc_now_us() - t0) > 30000U) {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    /* >=10 us trigger pulse */
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_HIGH);
    HAL_DelayUs(12U);
    HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_TRIG, GPIO_LOW);

    /* wait for echo to rise */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_LOW) {
        if ((sc_now_us() - t0) > timeout_us) {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    /* measure the high width */
    t0 = sc_now_us();
    while (HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_HIGH) {
        if ((sc_now_us() - t0) > timeout_us) {
            *status = SC_ST_TIMEOUT;
            return 0U;
        }
    }

    us = sc_now_us() - t0;

    /*
     * Below ~150 us (< 2.5 cm) nothing physical can echo: that is the trigger
     * pulse coupling into a floating / unpowered echo line.  Report it as a
     * failed measurement instead of a fake "obstacle 2 cm ahead".
     */
    if (us < 150U) {
        *status = SC_ST_TIMEOUT;

        return us;
    }

    return us;
}

/* --- RPMsg glue ----------------------------------------------------------- */
static void sc_timer_jitter(uint32_t iterations, uint32_t period_us,
                            uint32_t *min_us, uint32_t *max_us, uint32_t *avg_us)
{
    uint32_t i, t0, t1, d, lo = 0xFFFFFFFFU, hi = 0U;
    uint32_t sum = 0U;

    if (iterations == 0U || iterations > 20000U) {
        iterations = 1000U;
    }
    if (period_us == 0U || period_us > 100000U) {
        period_us = 100U;
    }

    for (i = 0U; i < iterations; i++) {
        t0 = sc_now_us();
        HAL_DelayUs(period_us);
        t1 = sc_now_us();
        d = t1 - t0;
        if (d < lo) {
            lo = d;
        }
        if (d > hi) {
            hi = d;
        }
        sum += d;
    }

    *min_us = lo;
    *max_us = hi;
    *avg_us = sum;              /* the caller (Linux) divides: saves a libcall */
}

/* --- timer jitter --------------------------------------------------------- */
static void sc_ns_cb(uint32_t new_ept, const char *new_ept_name, uint32_t flags, void *user_data)
{
    (void)user_data;
    (void)flags;
    /* our own printing: HAL_DBG() drops the varargs, and we want the ept number */
    sc_puts("[sc] ns '");
    sc_puts(new_ept_name ? new_ept_name : "?");
    sc_puts("' ept ");
    sc_putu(new_ept);
    sc_puts("\n");
}

static int32_t sc_rx_cb(void *payload, uint32_t payload_len, uint32_t src, void *priv)
{
    (void)priv;

    if (payload_len >= sizeof(struct sc_frame)) {
        memcpy(&sc_req, payload, sizeof(struct sc_frame));
        sc_master_ept = src;
        sc_rx_ready = 1;
    }

    return RL_RELEASE;
}

static void sc_send(struct sc_frame *f)
{
    if (rpmsg_lite_send(sc_inst, sc_ept, sc_master_ept, (char *)f,
                        sizeof(struct sc_frame), RL_BLOCK) != RL_SUCCESS) {
        HAL_DBG("sc: send failed\n");
    }
}

/* --- continuous sampling -------------------------------------------------- */

/* read the SR04 once and cache it */
static void sc_sample_sr04(void)
{
    uint32_t st = SC_ST_OK;
    uint32_t us = sc_sr04_read(SC_PERIODIC_SR04_TIMEOUT_US, &st);

    if (st == SC_ST_OK) {
        sc_sr04_echo_us = us;
        sc_sr04_mm = (us * 10U) / 58U;
        sc_sr04_ok = 1U;
        sc_sr04_bad = 0U;
    } else {
        sc_sr04_ok = 0U;
        sc_sr04_bad++;
        if (us != 0U && (sc_sr04_bad <= 3U || (sc_sr04_bad % 20U) == 0U)) {
            sc_puts("[sc] big echo=");
            sc_putu(us);
            sc_puts("us! ");
        }
    }
}

/* push the cached sample set to whoever last talked to us */
static void sc_push_sample(void)
{
    struct sc_frame ev;

    if (sc_master_ept == 0U) {
        return;                 /* nobody subscribed yet */
    }
    if (sc_sr04_period_ms == 0U && sc_dht_period_ms == 0U) {
        return;                 /* nothing enabled: nobody subscribed yet */
    }

    memset(&ev, 0, sizeof(ev));
    ev.cmd = SC_EVT_SAMPLE;
    ev.seq = (uint16_t)(sc_sample_seq++ & 0xFFFFU);
    ev.status = (sc_sr04_ok ? SC_EVT_SR04_VALID : 0U) |
                (sc_dht_fresh ? SC_EVT_DHT_VALID : 0U);
    ev.a = sc_sr04_echo_us;
    ev.b = sc_sr04_mm;
    ev.c = ((sc_dht_temp_x10 & 0xFFFFU) << 16) | (sc_dht_hum_x10 & 0xFFFFU);
    ev.t = sc_now_us();

    sc_send(&ev);
    sc_push_count++;
}

/*
 * 车机本地 LED / 倒车蜂鸣的服务：实现见 sc_carctl.h 里的 sc_service_carctl()
 * （抽出去是为了能在电脑上跑时序测试，固件与测试共用同一份实现）。
 */

/*
 * Periodic work: sample the enabled sensors when they are due and push the
 * cached set.  Never blocks longer than one sensor read (SR04 <= 30 ms timeout,
 * DHT11 ~20 ms start pulse + 5 ms bits).
 */
static void sc_service_tick(void)
{
    uint32_t now = sc_now_us();
    int did = 0;

    /* ---- 车机本地：转向灯 LED（1 kHz 软 PWM + 1.5 Hz 闪）与倒车蜂鸣 ---- */
    sc_service_carctl(now);

    /* non-blocking buzzer tone (square wave toggled from here) */
    if (sc_tone_freq != 0U && (int32_t)(now - sc_tone_due_us) >= 0) {
        uint32_t half = 500000U / sc_tone_freq;

        if (half == 0U) {
            half = 1U;
        }
        sc_tone_level ^= 1U;
        /* buzzer is active low */
        HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER,
                             sc_tone_level ? GPIO_LOW : GPIO_HIGH);
        sc_tone_due_us = now + half;
    }

    if (sc_sr04_period_ms != 0U && (int32_t)(now - sc_sr04_due_us) >= 0) {
        sc_sample_sr04();
        sc_sr04_due_us = sc_now_us() + sc_sr04_period_ms * 1000U;
        did = 1;
        sc_log_sample();
    }

    if (sc_dht_period_ms != 0U && (int32_t)(now - sc_dht_due_us) >= 0) {
        sc_sample_dht();
        /*
         * A failed attempt is retried much sooner than the configured period,
         * so a single bad checksum costs 500 ms instead of a whole period.  The
         * cached value stays valid meanwhile (SC_DHT_GRACE_US).
         */
        if (sc_dht_fail == 0U) {
            sc_dht_due_us = sc_now_us() + sc_dht_period_ms * 1000U;
        } else {
            sc_dht_due_us = sc_now_us() + SC_DHT_RETRY_US;
        }
        did = 1;
        /* no separate log line: the 10 Hz SR04 line already carries T/H */
    }

    if (did) {
        sc_push_sample();
    }
}

static void sc_handle(struct sc_frame *req, struct sc_frame *rsp)
{
    memset(rsp, 0, sizeof(*rsp));
    rsp->cmd = (uint16_t)(req->cmd | SC_RSP);
    rsp->seq = req->seq;
    rsp->status = SC_ST_OK;

    switch (req->cmd) {
    case SC_CMD_PING:
        break;

    case SC_CMD_TIME: {
        uint64_t t = HAL_GetSysTimerCount();

        rsp->a = (uint32_t)t;
        rsp->b = (uint32_t)(t >> 32);
        break;
    }

    case SC_CMD_JITTER:
        sc_timer_jitter(req->a, req->b, &rsp->a, &rsp->b, &rsp->c);
        break;

    case SC_CMD_SR04: {
        uint32_t to = req->a ? req->a : 30000U;
        uint32_t us = sc_sr04_read(to, &rsp->status);

        rsp->a = us;
        rsp->b = (us * 10U) / 58U;      /* echo us -> distance in mm */
        break;
    }

    case SC_CMD_DHT11: {
        uint32_t t, h;

        if (sc_dht11_read(&t, &h, &rsp->status)) {
            rsp->a = t;         /* temperature x10 */
            rsp->b = h;         /* humidity x10 */
            sc_dht_temp_x10 = t;
            sc_dht_hum_x10 = h;
            sc_dht_ok = 1U;
            sc_dht_fresh = 1U;
            sc_dht_last_ok_us = sc_now_us();
            sc_dht_fail = 0U;
        }
        break;
    }

    case SC_CMD_INFO:
        rsp->a = SC_FW_VERSION;
        rsp->b = (uint32_t)((char *)__heap_end - (char *)__heap_start);
        rsp->c = sc_cnt_per_us;
        break;

    case SC_CMD_SUBSCRIBE:
        /* a = SR04 period ms, b = DHT11 period ms (0 disables that sensor) */
        if (req->a > 60000U || req->b > 60000U) {
            rsp->status = SC_ST_BADARG;
            break;
        }
        sc_sr04_period_ms = req->a;
        sc_dht_period_ms = req->b;
        sc_puts(" sub sr04=");
        sc_putu(sc_sr04_period_ms);
        sc_puts("ms dht11=");
        sc_putu(sc_dht_period_ms);
        sc_puts("ms\n");
        /* sample once right away so the subscriber sees data immediately */
        if (sc_sr04_period_ms != 0U) {
            sc_sample_sr04();
            sc_sr04_due_us = sc_now_us() + sc_sr04_period_ms * 1000U;
        } else {
            sc_sr04_ok = 0U;
        }
        if (sc_dht_period_ms != 0U) {
            sc_sample_dht();
            sc_dht_due_us = sc_now_us() + sc_dht_period_ms * 1000U;
        } else {
            sc_dht_ok = 0U;
            sc_dht_fresh = 0U;      /* disabled: stop reporting the cached value */
        }
        /* fall through to reporting the cache */
        /* no break on purpose */

    case SC_CMD_LATEST:
        if (req->cmd == SC_CMD_LATEST && sc_sr04_period_ms == 0U) {
            /* no subscription: take one shot now (SR04 is cheap, < 20 ms) */
            sc_sample_sr04();
        }
        /*
         * The DHT11 is deliberately NOT read here: a read costs ~25 ms and the
         * part needs >= 1 s between conversions, so LATEST only reports the
         * cache.  Use 0x0006 DHT11 for an explicit fresh read.
         */
        rsp->status = (sc_sr04_ok ? SC_EVT_SR04_VALID : 0U) |
                      (sc_dht_valid() ? SC_EVT_DHT_VALID : 0U);
        rsp->a = sc_sr04_mm;
        rsp->b = sc_dht_temp_x10;
        rsp->c = sc_dht_hum_x10;
        break;

    case SC_CMD_PUSH_DHT:
        /*
         * The A7 owns the DHT11 and tells us what it read, so this console can
         * print distance + temperature + humidity on the same line.  Values are
         * kept for SC_DHT_GRACE_US; after that they are shown as "--.-" again.
         */
        if (req->a != 0U || req->b != 0U) {
            sc_dht_temp_x10 = req->a;       /* temperature x10 */
            sc_dht_hum_x10 = req->b;        /* humidity x10 */
            sc_dht_from_a7 = 1U;
            sc_dht_a7_us = sc_now_us();
            sc_dht_fresh = 1U;
            sc_dht_ok = 1U;
            sc_dht_fail = 0U;
        }
        rsp->a = sc_dht_temp_x10;
        rsp->b = sc_dht_hum_x10;
        break;

    case SC_CMD_TONE:
        /* a = frequency in Hz, 0 stops the tone */
        if (req->a > 20000U) {
            rsp->status = SC_ST_BADARG;
            break;
        }
        sc_tone_freq = req->a;
        if (sc_tone_freq == 0U) {
            sc_tone_level = 0U;
            HAL_GPIO_SetPinLevel(GPIO1, SC_PIN_BUZZER, GPIO_HIGH);
        } else {
            sc_tone_due_us = sc_now_us();
        }
        rsp->a = sc_tone_freq;
        break;

    case SC_CMD_CARCTL:
        /* a = 掩码：bit0 左转 bit1 右转 bit2 双闪 bit3 倒车 bit4 近光 bit5 远光
         * 立即生效（LED 由 sc_service_carctl 在 idle 里驱动）；回执 a = 已生效掩码。 */
        sc_car_mask = (uint8_t)(req->a & 0x3FU);
        sc_carctl_reset(sc_now_us());          /* 复位 PWM/闪烁/蜂鸣时序（见 sc_carctl.h） */
        rsp->a = sc_car_mask;
        break;

    default:
        rsp->status = SC_ST_BADARG;
        break;
    }

    rsp->t = sc_now_us();
}

void sc_amp_run(void)
{
    sc_probe_magic = 0x414D5042U;
    sc_probe_step = 0U;

    sc_time_init();
    SC_PROBE(1);

    sc_pins_init();
    SC_PROBE(2);
    /* GPIO1_C4 is deliberately not touched: the A7 owns the DHT11 there */

    sc_probe_extra = (uint32_t)((char *)__heap_end - (char *)__heap_start);

    /*
     * Print the real numbers, not HAL_DBG("...%u...", x): the SDK's HAL_DBG
     * macro ignores its varargs, so a formatted string came out as
     * "sc: timer  counts/us, heap  bytes" on the console.  sc_putu() is ours.
     */
    sc_puts("[sc] timer ");
    sc_putu(sc_cnt_per_us);
    sc_puts(" counts/us, heap ");
    sc_putu(sc_probe_extra);
    sc_puts(" bytes\n");

    sc_inst = rpmsg_lite_remote_init((void *)&__linux_share_rpmsg_start__[0],
                                     RL_PLATFORM_SET_LINK_ID(SC_MASTER_ID, SC_REMOTE_ID),
                                     RL_NO_FLAGS);
    if (sc_inst == ((void *)0)) {
        SC_PROBE_FAIL(1);
        HAL_DBG("sc: rpmsg_lite_remote_init failed\n");
        return;
    }
    SC_PROBE(3);

    rpmsg_lite_wait_for_link_up(sc_inst);
    SC_PROBE(4);
    sc_puts("[sc] link ");
    sc_putu(sc_inst->link_id);
    sc_puts(")\n");

    rpmsg_ns_bind(sc_inst, sc_ns_cb, (void *)0);
    sc_ept = rpmsg_lite_create_ept(sc_inst, SC_EPT_ID, sc_rx_cb, (void *)0);
    if (sc_ept == ((void *)0)) {
        SC_PROBE_FAIL(2);
        HAL_DBG("sc: create_ept failed\n");
        return;
    }
    SC_PROBE(5);

    rpmsg_ns_announce(sc_inst, sc_ept, SC_EPT_NAME, RL_NS_CREATE);
    SC_PROBE(6);
    sc_puts("[sc] svc '");
    sc_puts(SC_EPT_NAME);
    sc_puts("' up sr04=");
    sc_putu(sc_sr04_period_ms);
    sc_puts("ms dht11=");
    sc_putu(sc_dht_period_ms);
    sc_puts("ms\n");

    /*
     * 启动横幅（2026-09-29 精简）
     * --------------------------
     * 原来这里是 8 行很长的说明文字（约 700 字节 .rodata）。加了车机 LED/倒车
     * 蜂鸣之后 32 KB TCM 放不下了（ld: cannot move location counter backwards），
     * 而这些都是纯诊断信息 —— 引脚/线序在 docs/接线与使用手册.md 里写得更清楚，
     * 所以压成两行，代码功能一字未改。
     */
    sc_puts("\n[sc] M0 ready: sr04 C6/C7, buzzer D0(lo), ledL C0, ledR C1\n");
    sc_puts("[sc] echo=");
    sc_putu((HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_ECHO) == GPIO_HIGH) ? 1U : 0U);
    sc_puts(" dht=");
    sc_putu((HAL_GPIO_GetPinLevel(GPIO1, SC_PIN_DHT) == GPIO_HIGH) ? 1U : 0U);
    sc_puts(" heap=");
    sc_putu(sc_probe_extra);
    sc_puts("B\n");

    /* start the periodic sampling right away, subscriber or not */
    sc_sr04_due_us = sc_now_us();
    /* the DHT11 needs >= 1 s to settle after power-up before it will answer */
    sc_dht_due_us = sc_now_us() + 1200000U;

    for (;;) {
        if (sc_rx_ready) {
            struct sc_frame rsp;

            sc_rx_ready = 0;
            sc_handle(&sc_req, &rsp);
            sc_send(&rsp);
            sc_probe_extra++;       /* served request counter */
        } else {
            /* nothing to serve: do the periodic sensor work */
            sc_service_tick();
        }
    }
}
