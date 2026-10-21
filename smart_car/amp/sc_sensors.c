/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sc_sensors - Linux side bridge for the RK3506 AMP split.
 *
 * Ownership (by design, see docs/AMP_M0实时串口观测_20260921.md):
 *
 *   Cortex-M0 : SR04 ultrasonic (TRIG/ECHO, microsecond timing)  -> /dev/mysr04
 *               buzzer square wave                               -> /dev/mybuzzer
 *   A7 Linux  : DHT11 single-wire bit-bang (local, this module)   -> /dev/mydht11
 *
 * The DHT11 stays on the A7 because its data line is GPIO1_C4 and the M0 cannot
 * spend its 32 KiB on a two-second sensor; the ultrasonic needs the M0's
 * microsecond timing far more.  Set dht11_local=0 to get the old behaviour
 * (DHT11 sampled by the M0 and forwarded over rpmsg).
 *
 *   /dev/mysr04    read 4 bytes -> int echo width [us]   (M0, same ABI as before)
 *   /dev/mydht11   read 4 bytes -> hum_int, hum_dec, temp_int, temp_dec
 *   /dev/mybuzzer  write 2 bytes -> byte[1]=1 tone on, 0 off  (M0)
 *
 * It replaces smartcar_hw.ko for these sensors (the M0 owns the SR04 pins now),
 * so load this instead of that module.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/irqflags.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/io.h>

#define SC_EPT_NAME   "rpmsg-smartcar"
#define SC_EPT_ID     0x4004U           /* the M0's endpoint (announced) */
#define SC_RSP        0x8000U
#define SC_EVT_SAMPLE 0x8081U
#define SC_EVT_SR04_VALID 0x01U
#define SC_EVT_DHT_VALID  0x02U

#define SC_CMD_SUBSCRIBE 0x000AU
#define SC_CMD_TONE      0x000CU
#define SC_CMD_PUSH_DHT  0x000DU        /* A7 -> M0: a = temp*10, b = humidity*10 */
#define SC_CMD_CARCTL    0x000EU        /* A7 -> M0: a = 车机本地掩码（/dev/mycarctl）
                                         *   bit0 左转 bit1 右转 bit2 双闪 bit3 倒车
                                         *   bit4 近光 bit5 远光；回执 a = 已生效掩码 */

struct sc_frame {
	u16 cmd;
	u16 seq;
	u32 status;
	u32 a;
	u32 b;
	u32 c;
	u32 t;
} __packed;

static struct rpmsg_device *sc_rpdev;
static DEFINE_MUTEX(sc_lock);

static u32 sc_echo_us;
static u32 sc_dist_mm;
static u32 sc_temp_x10;
static u32 sc_hum_x10;
static u32 sc_status;
static u64 sc_last_jiffies;
static u32 sc_rx_count;
static u32 sc_tone_hz;
/* 车机本地掩码（/dev/mycarctl）：写入值 + M0 回执（声明要在 sc_callback 之前） */
static u8  sc_car_mask;
static u8  sc_car_echo;
static bool sc_car_echo_valid;

/* --- rpmsg TX: blocking vs non-blocking ----------------------------------
 *
 * Measured on this board (docs/超声波RCWL1601无示数_排查_20261013.md §2.3): when
 * the M0 stops harvesting its RX vring, the virtio-rpmsg layer runs its TX
 * buffer out and **blocks for the full 500 ms timeout** before returning
 * -ETIMEDOUT (-512):
 *
 *     rpmsg_sendto(0x4004) failed: -512
 *
 * The vring indices were sampled 20 times, one second apart, and not a single
 * bit moved (vq0 avail / vq1 used), i.e. the M0 really is not draining.  So the
 * blocking call always pays the whole 500 ms.
 *
 * Where that used to hurt:
 *   - sc_dht_poll_work(): the 2 s autonomous DHT sampler did one blocking push
 *     per reading, so whenever the M0 was deaf the sampler burned 500 ms per
 *     cycle.  Observed end state: `self-poll #315 ... pushed 58 ... err 257`,
 *     `app reads in 5 s: mysr04 1 (0.2 Hz), mydht11 1 (0.2 Hz)`.
 *   - carctl_write(): 3 x blocking send => up to 1.5 s **inside the write()
 *     syscall**, with the workqueue's 500 ms also on the CPU.  That is a
 *     mutex-holding stall in the call path of the app that drives the turn
 *     signals - exactly the "board went unresponsive" symptom.
 *
 * So: routine traffic uses rpmsg_trysendto() (never waits for a TX buffer) and
 * degrades to "skip this frame" instead of stalling.  A dropped DHT push or
 * carctl frame is cheap - both are retried naturally (the sampler comes back
 * 2 s later, and carctl_read() reports the last M0 echo).
 *
 * Only the name-service SUBSCRIBE keeps the blocking call: it must go out at
 * probe/remove time, there is no retry path for it, and a 500 ms cost at
 * module load is irrelevant.
 */

/* Send explicitly to the M0's endpoint.  rpmsg_send() would use rpdev->dst,
 * which for a channel created by the remote's name-service announcement is not
 * the remote endpoint address - that is why the first version of this driver
 * subscribed into the void. */
static int sc_send_to(struct sc_frame *f, bool blocking)
{
	if (!sc_rpdev || !sc_rpdev->ept)
		return -ENODEV;

	if (blocking)
		return rpmsg_sendto(sc_rpdev->ept, f, sizeof(*f), SC_EPT_ID);

	return rpmsg_trysendto(sc_rpdev->ept, f, sizeof(*f), SC_EPT_ID);
}

/* Probe/remove only: this one is allowed to wait. */
static int sc_send(struct sc_frame *f)
{
	int ret = sc_send_to(f, true);

	if (ret)
		dev_err(&sc_rpdev->dev, "rpmsg_sendto(0x%x) failed: %d\n", SC_EPT_ID, ret);

	return ret;
}

/*
 * Non-blocking with a small bounded retry: µs-scale delay, so the caller can
 * never stall for more than a few ms even when the M0 is deaf.
 *
 * Returns 0 on success, the last error otherwise.  -EBUSY is returned as soon
 * as the link is known to be dead, for callers that want to back off.  The log
 * is rate limited: with a dead M0 this path is hit on every attempt, and an
 * unthrottled dev_err() here would just move the log storm from bluetoothd to
 * this driver.
 *
 * NOT `static`: keep GCC from inlining the body three times (the file's own
 * style note at sc_rate_account() is about the same worry).
 */
int sc_send_retry(struct sc_frame *f, int tries)
{
	int ret = -ENODEV;

	if (!sc_rpdev || !sc_rpdev->ept)
		return ret;

	if (tries < 1)
		tries = 1;

	while (tries-- > 0) {
		ret = rpmsg_trysendto(sc_rpdev->ept, f, sizeof(*f), SC_EPT_ID);
		if (!ret)
			return 0;
		if (ret == -EBUSY)
			return ret;             /* TX vring empty of buffers */
		udelay(200);
	}

	dev_err_ratelimited(&sc_rpdev->dev,
			    "rpmsg_trysendto(0x%x) failed: %d\n", SC_EPT_ID, ret);
	return ret;
}

static void sc_subscribe(u32 sr04_ms, u32 dht_ms)
{
	struct sc_frame f;
	memset(&f, 0, sizeof(f));
	f.cmd = SC_CMD_SUBSCRIBE;
	f.seq = 1;
	f.a = sr04_ms;
	f.b = dht_ms;
	sc_send(&f);
}

/*
 * Tell the M0 what we just read from the DHT11.
 *
 * The sensor stays on the A7 (its bit timing is far more reliable than the M0's
 * - see docs/模块职责_20260921.md 第 7 章), but the M0's UART4 console is the one
 * place where distance + temperature + humidity can be seen together, so push
 * each fresh reading over.  Rate limited to once per second: the part cannot
 * convert faster than that anyway.
 *
 * Returns 0 when the frame went out, -EAGAIN when it was skipped on purpose
 * (nothing to send, or the 1 Hz limiter), or the rpmsg error otherwise - the
 * autonomous poller counts these so dmesg can prove the push really happens.
 */
static int sc_push_dht(u32 temp_x10, u32 hum_x10)
{
	struct sc_frame f;
	static unsigned long last_push;
	/*
	 * Exponential backoff while the M0 is deaf.  Without it the sampler keeps
	 * hammering a dead TX vring and every attempt drags the blocking 500 ms TX
	 * timeout into the workqueue (see the rpmsg TX note above).  Starts at the
	 * normal 1 s cadence and doubles per consecutive failure, capped at 60 s,
	 * so a dead M0 costs nothing and a recovered one is noticed within 60 s.
	 */
	static unsigned int fail_streak;
	unsigned long wait = HZ;
	int ret;

	if (temp_x10 == 0U && hum_x10 == 0U)
		return -EAGAIN;

	wait <<= (fail_streak > 6U) ? 6U : fail_streak;      /* 1,2,4,...,64 s */
	if (wait > 60UL * HZ)
		wait = 60UL * HZ;
	if (last_push && time_before(jiffies, last_push + wait))
		return -EAGAIN;
	last_push = jiffies;

	memset(&f, 0, sizeof(f));
	f.cmd = SC_CMD_PUSH_DHT;
	f.seq = 4;
	f.a = temp_x10;
	f.b = hum_x10;

	/* Routine traffic: never wait for a TX buffer. */
	ret = sc_send_retry(&f, 3);
	if (ret) {
		if (fail_streak < 32U)
			fail_streak++;
	} else {
		fail_streak = 0;
	}

	return ret;
}

static int sc_callback(struct rpmsg_device *rpdev, void *data, int len,
		       void *priv, u32 src)
{
	struct sc_frame *f = data;

	if (len < (int)sizeof(*f))
		return 0;
	/* 车机本地掩码的回执（A7 读 /dev/mycarctl 拿到它） */
	if ((f->cmd & 0x8000U) && (f->cmd & 0x7FFFU) == SC_CMD_CARCTL) {
		mutex_lock(&sc_lock);
		sc_car_echo = (u8)(f->a & 0xFFU);
		sc_car_echo_valid = true;
		mutex_unlock(&sc_lock);
		return 0;
	}
	if (f->cmd != SC_EVT_SAMPLE)
		return 0;

	mutex_lock(&sc_lock);
	sc_status = f->status;
	sc_echo_us = f->a;
	sc_dist_mm = f->b;
	sc_temp_x10 = (f->c >> 16) & 0xFFFFU;
	sc_hum_x10 = f->c & 0xFFFFU;
	sc_last_jiffies = jiffies;
	sc_rx_count++;
	mutex_unlock(&sc_lock);

	return 0;
}

/* --- DHT11 on the A7: single-wire bit-bang -------------------------------
 *
 * The sensor answers with 80 us low / 80 us high and then 40 bits encoded as a
 * ~50 us low followed by a 26-28 us (0) or 70 us (1) high, so the bit reader
 * must run with interrupts disabled and time itself with ktime.
 *
 * The *pad* is prepared once by the M0 firmware at boot: RK3506 routes
 * GPIO1_C2/C3/C4 through the RMIO matrix which Linux cannot reach, and a
 * single-wire bus needs a pull-up, so the M0 leaves C4 as a plain GPIO input
 * with the SoC pull-up enabled and then never touches it again.
 */
#define DHT11_GPIO_DEFAULT 52           /* GPIO1_C4 */

static int dht11_local = 1;
module_param(dht11_local, int, 0644);
MODULE_PARM_DESC(dht11_local, "1 = bit-bang the DHT11 here (default), 0 = use the M0's cache");
static int dht11_pin = DHT11_GPIO_DEFAULT;
module_param(dht11_pin, int, 0644);
MODULE_PARM_DESC(dht11_pin, "DHT11 data GPIO number (default 52 = GPIO1_C4)");
/*
 * Poll period of the autonomous sampler, in ms (0 = off).  The M0 prints
 * distance + temperature + humidity on its own console line, so it needs the
 * T/H values even when nothing reads /dev/mydht11 (fresh boot, no app): the
 * module therefore samples the sensor on its own and pushes each reading over
 * rpmsg.  2000 ms matches the DHT11's >= 1 s sampling requirement with margin.
 */
static int dht11_poll_ms = 2000;
module_param(dht11_poll_ms, int, 0644);
MODULE_PARM_DESC(dht11_poll_ms, "autonomous DHT11 sample+push period in ms, 0 = off");

/*
 * --- steady-state health lines: silent by default ------------------------
 *
 * Two lines prove the link works and then become pure noise for the lifetime of
 * the board: "app reads in 5 s" (every 5 s) and "self-poll #N" (every 30 s).
 * They are logged at debug level now - the same lines dynamic debug calls
 * pr_debug()/dev_dbg() - and nothing else changed: the counters, the state
 * changes (probe/remove, first failure, recovery) and every error path still
 * print at their old KERN_INFO/KERN_ERR/KERN_WARNING levels.
 *
 * This kernel is built without CONFIG_DYNAMIC_DEBUG (there is no
 * /sys/kernel/debug/dynamic_debug/control on the board), so the one-line
 * runtime switch is this parameter instead:
 *
 *     insmod sc_sensors.ko ... sc_verbose=1
 *     echo 1 > /sys/module/sc_sensors/parameters/sc_verbose     # on
 *     echo 0 > /sys/module/sc_sensors/parameters/sc_verbose     # off again
 *
 * With that set, exactly the old lines come back at KERN_INFO.  On a kernel
 * built with CONFIG_DYNAMIC_DEBUG the dev_dbg()/pr_debug() branch is what
 *     echo 'file sc_sensors.c +p' > /sys/kernel/debug/dynamic_debug/control
 * would turn on, no rebuild needed.
 */
static bool sc_verbose;
module_param(sc_verbose, bool, 0644);
MODULE_PARM_DESC(sc_verbose, "1 = print the 5 s read-rate / 30 s self-poll health lines (default 0 = quiet)");

/* The DHT poller starts at module_init(), possibly before the rpmsg channel is
 * announced, so sc_rpdev may still be NULL: fall back to pr_*() there. */
#define sc_dev_err(fmt, ...)						\
	do {								\
		if (sc_rpdev)						\
			dev_err(&sc_rpdev->dev, fmt, ##__VA_ARGS__);	\
		else							\
			pr_err("sc_sensors: " fmt, ##__VA_ARGS__);	\
	} while (0)

#define sc_dev_info(fmt, ...)						\
	do {								\
		if (sc_rpdev)						\
			dev_info(&sc_rpdev->dev, fmt, ##__VA_ARGS__);	\
		else							\
			pr_info("sc_sensors: " fmt, ##__VA_ARGS__);	\
	} while (0)

/* one steady-state health line: dev_dbg() level, KERN_INFO when sc_verbose */
#define sc_health(fmt, ...)						\
	do {								\
		if (sc_verbose)						\
			pr_info(fmt, ##__VA_ARGS__);			\
		else if (sc_rpdev)					\
			dev_dbg(&sc_rpdev->dev, fmt, ##__VA_ARGS__);	\
		else							\
			pr_debug(fmt, ##__VA_ARGS__);			\
	} while (0)

static DEFINE_MUTEX(dht11_lock);
static int dht11_gpio = -1;
static unsigned char dht11_cache[4];
static unsigned long dht11_cache_jiffies;
static int dht11_cache_valid;
static unsigned int dht11_fail;         /* consecutive failures */
static unsigned int dht11_idle = 2;     /* line level before the start pulse */
static unsigned int dht11_stage;        /* 1 handshake, 2 bits */
static unsigned int dht11_idx;
static unsigned int dht11_wlow, dht11_whigh;

/*
 * --- GPIO1_C4 pad bias, programmed directly -------------------------------
 *
 * Measured on this board: the pad comes up **pull-down** (GPIO1C_PULL read
 * 0x0000AA4A => bits[9:8] == 2 for C4), so the single-wire bus is held at 0 and
 * the DHT11 can never answer ("idle level 0 - BUS LOW").  On the previous AMP
 * firmware the *M0* enabled the pull-up with HAL_PINCTRL_SetParam(PUL_UP); that
 * call is the whole generic pinconf path (~3.6 KiB) and no longer fits in the
 * M0's 32 KiB TCM, so the A7 does it here instead.
 *
 * pinctrl_gpio_set_config(PIN_CONFIG_BIAS_PULL_UP) cannot be used: the gpio has
 * no pinconf mapping on this SoC and the call returns -ENOTSUPP.  Write the IOC
 * register instead: GPIO1_IOC (0xff660000) + GPIO1C_PULL (0x218), 2 bits per
 * pin, write-enable in the upper half word, C4 -> bits [9:8].
 * RK3506 encoding: 0=disable 1=pull-up 2=pull-down 3=keep.
 */
#define SC_GPIO1C_PULL_PHYS     0xff660218UL
#define SC_GPIO1C4_PULL_UP      0x03000100U     /* enable bit8 field + value 1 */

static int sc_dht_pad_pull_up(int gpio)
{
	void __iomem *reg = ioremap(SC_GPIO1C_PULL_PHYS, 4);
	u32 val;

	if (!reg)
		return -ENOMEM;
	writel(SC_GPIO1C4_PULL_UP, reg);
	val = readl(reg);
	iounmap(reg);

	return ((val >> 8) & 0x3) == 1 ? 0 : -EIO;
}

/* wait until the line reads `level`; returns 0, or -ETIMEDOUT after timeout_us */
static int dht11_wait(int level, unsigned int timeout_us, unsigned int *width)
{
	u64 t0 = ktime_get_ns();
	u64 t1;
	u32 dt_ns;

	for (;;) {
		if (gpio_get_value(dht11_gpio) == level)
			break;
		t1 = ktime_get_ns();
		if ((t1 - t0) > (u64)timeout_us * 1000ULL) {
			if (width)
				*width = (unsigned int)((u32)(t1 - t0) / 1000U);
			return -ETIMEDOUT;
		}
	}

	/*
	 * The loop above can only exit within timeout_us, so the delta fits in
	 * 32 bits: dividing as u32 keeps the ARM 64-bit division helper
	 * (__aeabi_uldivmod) out of the module.
	 */
	t1 = ktime_get_ns();
	dt_ns = (u32)(t1 - t0);
	if (width)
		*width = (unsigned int)(dt_ns / 1000U);

	return 0;
}

/* one full transaction; out[] = hum_int, hum_dec, temp_int, temp_dec, checksum */
static int dht11_hw_read(unsigned char out[5])
{
	unsigned long flags;
	unsigned int bits[40];
	unsigned int w;
	int i, ret = -EIO;

	mutex_lock(&dht11_lock);

	/*
	 * The part must not be converted faster than once per second.  Both the
	 * /dev/mydht11 reader and the autonomous poller come through here, so
	 * reusing the last good frame is what keeps the two of them from
	 * hammering the sensor (a too-fast start pulse yields bad checksums).
	 */
	if (dht11_cache_valid && time_before(jiffies, dht11_cache_jiffies + HZ)) {
		memcpy(out, dht11_cache, 4);
		out[4] = (unsigned char)((out[0] + out[1] + out[2] + out[3]) & 0xFFU);
		mutex_unlock(&dht11_lock);
		return 0;
	}

	/*
	 * The M0 handed this pad over as an input with the internal pull-up on;
	 * enabling it again here is harmless and documents the dependency.
	 */
	gpio_direction_input(dht11_gpio);
	dht11_idle = gpio_get_value(dht11_gpio);

	/* start signal: hold the bus low >= 18 ms, then release (may sleep) */
	if (gpio_direction_output(dht11_gpio, 0)) {
		ret = -EIO;
		goto out_unlock;
	}
	usleep_range(19000, 21000);
	gpio_direction_input(dht11_gpio);

	local_irq_save(flags);
	udelay(2);

	/* 80 us low, 80 us high, then the 40 bits */
	dht11_stage = 1;
	if (dht11_wait(0, 250, &w))              /* sensor pulls the bus low  */
		goto out_irq;
	if (dht11_wait(1, 250, &w))              /* ... for 80 us              */
		goto out_irq;
	if (dht11_wait(0, 250, &w))              /* ... then high for 80 us    */
		goto out_irq;

	dht11_stage = 2;
	for (i = 0; i < 40; i++) {
		dht11_idx = (unsigned int)i;
		if (dht11_wait(1, 250, &dht11_wlow))     /* 50 us low          */
			goto out_irq;
		if (dht11_wait(0, 250, &dht11_whigh))    /* then the data high */
			goto out_irq;
		bits[i] = dht11_whigh;
	}

	memset(out, 0, 5);
	for (i = 0; i < 40; i++) {
		out[i >> 3] = (unsigned char)(out[i >> 3] << 1);
		if (bits[i] > 40U)                      /* 26 us = 0, 70 us = 1 */
			out[i >> 3] |= 1U;
	}
	ret = 0;

out_irq:
	local_irq_restore(flags);
out_unlock:
	mutex_unlock(&dht11_lock);

	return ret;
}

static void dht11_report_failure(int ret, unsigned char chk_ok)
{
	dht11_fail++;
	if (dht11_fail > 3U && (dht11_fail % 15U) != 0U)
		return;                         /* keep dmesg readable */

	pr_warn("sc_sensors: dht11 fail (%d) idle=%u stage=%u idx=%u low=%uus high=%uus checksum=%s\n",
		ret, dht11_idle, dht11_stage, dht11_idx, dht11_wlow, dht11_whigh,
		chk_ok ? "ok" : "bad");
	if (dht11_idle == 0U)
		pr_warn("sc_sensors:   bus is LOW - DHT11 unpowered, DATA not on gpio %d, or shorted\n",
			dht11_pin);
	else
		pr_warn("sc_sensors:   bus is HIGH but the chip does not answer - dead sensor or not a DHT11\n");
}

/* remember a validated frame and hand it straight to the M0 */
static int dht11_commit(const unsigned char raw[4])
{
	mutex_lock(&dht11_lock);
	memcpy(dht11_cache, raw, 4);
	dht11_cache_jiffies = jiffies;
	dht11_cache_valid = 1;
	mutex_unlock(&dht11_lock);

	if (dht11_fail) {
		pr_info("sc_sensors: dht11 recovered after %u failures (%u.%u C / %u.%u %%)\n",
			dht11_fail, raw[2], raw[3], raw[0], raw[1]);
		dht11_fail = 0;
	}

	/*
	 * Hand the fresh reading to the M0: its UART4 console is the only place
	 * where distance + temperature + humidity appear on one line.
	 */
	return sc_push_dht((u32)raw[2] * 10U + raw[3], (u32)raw[0] * 10U + raw[1]);
}

/* counters for the autonomous sampler - read out through dmesg */
static unsigned int sc_poll_runs;
static unsigned int sc_poll_hw_ok;
static unsigned int sc_poll_hw_fail;
static unsigned int sc_poll_pushed;
static unsigned int sc_poll_skip;
static unsigned int sc_poll_err;
static unsigned int sc_poll_err_streak;  /* consecutive push failures, for logging */
static unsigned int sc_poll_last_t, sc_poll_last_h;

static struct delayed_work sc_dht_work;

/*
 * Autonomous sampler.  The M0 prints T/H on its own console line, so it must
 * receive a level even when nothing opens /dev/mydht11 - right after a power
 * cycle, or with the Qt app not started yet.  Without this the console shows
 * "--.-" for temperature and humidity until something reads the node.
 */
static void sc_dht_poll_work(struct work_struct *work)
{
	unsigned char raw[5];
	int ret;

	sc_poll_runs++;

	if (!dht11_local || dht11_gpio < 0) {
		sc_poll_skip++;
		goto resched;
	}

	ret = dht11_hw_read(raw);
	if (ret) {
		sc_poll_hw_fail++;
		dht11_report_failure(ret, 1);
		goto resched;
	}
	if (((raw[0] + raw[1] + raw[2] + raw[3]) & 0xFFU) != raw[4]) {
		sc_poll_hw_fail++;
		dht11_report_failure(-EBADMSG, 0);
		goto resched;
	}

	sc_poll_hw_ok++;
	sc_poll_last_t = (unsigned int)raw[2] * 10U + raw[3];
	sc_poll_last_h = (unsigned int)raw[0] * 10U + raw[1];

	ret = dht11_commit(raw);
	if (ret == 0) {
		sc_poll_pushed++;
		/* state change: a real rpmsg failure streak ended */
		if (sc_poll_err_streak) {
			sc_dev_info("DHT push to the M0 recovered after %u failures\n",
				    sc_poll_err_streak);
			sc_poll_err_streak = 0;
		}
	} else if (ret == -EAGAIN) {
		sc_poll_skip++;         /* on purpose: nothing fresh, or the 1 Hz limiter */
	} else {
		sc_poll_err++;
		/*
		 * An actual rpmsg error of the autonomous push must never be
		 * silent - but logging it every 2 s while the M0 is deaf would
		 * just be a new log storm.  Print the first failure of a streak;
		 * the matching recovery line is above.
		 */
		if (sc_poll_err_streak++ == 0U)
			sc_dev_err("DHT push to the M0 failed: %d (further failures stay quiet until it recovers)\n",
				   ret);
	}

	/* proof of life: first few polls, then one line every 30 s (debug level) */
	if (sc_poll_runs <= 3U || (sc_poll_runs % 15U) == 0U)
		sc_health("sc_sensors: self-poll #%u: hw ok %u fail %u | pushed %u skipped %u err %u | last %u.%u C / %u.%u %%\n",
			  sc_poll_runs, sc_poll_hw_ok, sc_poll_hw_fail,
			  sc_poll_pushed, sc_poll_skip, sc_poll_err,
			  sc_poll_last_t / 10U, sc_poll_last_t % 10U,
			  sc_poll_last_h / 10U, sc_poll_last_h % 10U);

resched:
	if (dht11_poll_ms > 0)
		schedule_delayed_work(&sc_dht_work, msecs_to_jiffies(dht11_poll_ms));
}

/* --- read-rate instrumentation -------------------------------------------
 * The sensor side is fast (the M0 pushes a distance every 100 ms, the DHT11 is
 * a 1 Hz part); if the car UI looks sluggish we need to know how often the
 * application actually reads these nodes, so count reads and report the rate to
 * dmesg every 5 s.
 */
static unsigned int sc_sr04_reads;
static unsigned int sc_dht_reads;
static unsigned long sc_rate_jiffies;
static unsigned int sc_rate_last_sr04, sc_rate_last_dht;

static void sc_rate_account(int is_sr04)
{
	if (is_sr04)
		sc_sr04_reads++;
	else
		sc_dht_reads++;

	if (!sc_rate_jiffies)
		sc_rate_jiffies = jiffies;
	if (time_before(jiffies, sc_rate_jiffies + 5 * HZ))
		return;

	{
		/* integer only: doubles would pull libgcc's __aeabi_ddiv into the module */
		unsigned int ds = sc_sr04_reads - sc_rate_last_sr04;
		unsigned int dd = sc_dht_reads - sc_rate_last_dht;

		sc_health("sc_sensors: app reads in 5 s: mysr04 %u (%u.%u Hz), mydht11 %u (%u.%u Hz)\n",
			  ds, ds / 5U, (ds % 5U) * 2U,
			  dd, dd / 5U, (dd % 5U) * 2U);
	}
	sc_rate_last_sr04 = sc_sr04_reads;
	sc_rate_last_dht = sc_dht_reads;
	sc_rate_jiffies = jiffies;
}

/* --- /dev/mysr04 : int echo width in us ---------------------------------- */
static ssize_t sr04_read(struct file *fp, char __user *buf, size_t len, loff_t *off)
{
	int val;
	u32 st;

	sc_rate_account(1);
	if (len < sizeof(int))
		return -EINVAL;

	mutex_lock(&sc_lock);
	st = sc_status;
	val = (int)sc_echo_us;
	mutex_unlock(&sc_lock);

	if (!(st & SC_EVT_SR04_VALID))
		return -EIO;
	if (copy_to_user(buf, &val, sizeof(int)))
		return -EFAULT;

	return sizeof(int);
}

static const struct file_operations sr04_fops = {
	.owner = THIS_MODULE,
	.read = sr04_read,
};

static struct miscdevice sr04_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mysr04",
	.fops = &sr04_fops,
	.mode = 0600,
};

/* --- /dev/mydht11 : hum_int, hum_dec, temp_int, temp_dec ------------------ */
static ssize_t dht11_read(struct file *fp, char __user *buf, size_t len, loff_t *off)
{
	unsigned char out[4];
	unsigned char raw[5];
	u32 st, t, h;
	int ret;

	sc_rate_account(0);
	if (len < 4)
		return -EINVAL;

	if (!dht11_local) {
		/* the M0 sampled it for us and pushed the value over rpmsg */
		mutex_lock(&sc_lock);
		st = sc_status;
		t = sc_temp_x10;
		h = sc_hum_x10;
		mutex_unlock(&sc_lock);

		if (!(st & SC_EVT_DHT_VALID))
			return -EIO;

		out[0] = (unsigned char)(h / 10U);      /* humidity integer */
		out[1] = (unsigned char)(h % 10U);      /* humidity decimal */
		out[2] = (unsigned char)(t / 10U);      /* temperature integer */
		out[3] = (unsigned char)(t % 10U);      /* temperature decimal */
		goto out_copy;
	}

	/* DHT11 wants >= 1 s between transactions: serve the cache in between */
	if (dht11_cache_valid && time_before(jiffies, dht11_cache_jiffies + HZ)) {
		memcpy(out, dht11_cache, 4);
		goto out_copy;
	}

	ret = dht11_hw_read(raw);
	if (ret) {
		/* one retry: clones frequently miss the first transaction */
		usleep_range(5000, 6000);
		ret = dht11_hw_read(raw);
	}
	if (ret) {
		dht11_report_failure(ret, 1);
		if (dht11_cache_valid && time_before(jiffies, dht11_cache_jiffies + 30U * HZ)) {
			memcpy(out, dht11_cache, 4);
			goto out_copy;
		}

		return -EIO;
	}

	if (((raw[0] + raw[1] + raw[2] + raw[3]) & 0xFFU) != raw[4]) {
		dht11_report_failure(-EBADMSG, 0);
		if (dht11_cache_valid && time_before(jiffies, dht11_cache_jiffies + 30U * HZ)) {
			memcpy(out, dht11_cache, 4);
			goto out_copy;
		}

		return -EIO;
	}

	memcpy(out, raw, 4);
	dht11_commit(raw);

out_copy:
	if (copy_to_user(buf, out, 4))
		return -EFAULT;

	return 4;
}

static const struct file_operations dht11_fops = {
	.owner = THIS_MODULE,
	.read = dht11_read,
};

static struct miscdevice dht11_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mydht11",
	.fops = &dht11_fops,
	.mode = 0600,
};

/* --- /dev/mybuzzer : byte[1] = 1 -> tone on ------------------------------ */
static ssize_t buzzer_write(struct file *fp, const char __user *buf, size_t len,
			    loff_t *off)
{
	unsigned char tmp[2] = { 0, 0 };
	struct sc_frame f;

	if (len < 2)
		return -EINVAL;
	if (copy_from_user(tmp, buf, 2))
		return -EFAULT;

	mutex_lock(&sc_lock);
	sc_tone_hz = tmp[1] ? 2000U : 0U;
	mutex_unlock(&sc_lock);

	memset(&f, 0, sizeof(f));
	f.cmd = SC_CMD_TONE;
	f.seq = 2;
	f.a = sc_tone_hz;

	/*
	 * ★ 2026-10-13（F2）：这里原来也是阻塞发送（等满 500 ms 超时），
	 * 而它的调用方是"倒车滴滴 / 转向蜂鸣"这类高频路径 —— 一旦 M0 耳聋，
	 * 每次拨号都会在 write() 里卡 500 ms。改成非阻塞后最坏约 1 ms。
	 * 但**保留 -EIO 语义**：蜂鸣器写不出去就是真的没响，调用方有权知道
	 * （与 carctl 不同，那里有 M0 回执兜底）。
	 */
	if (sc_send_retry(&f, 6))
		return -EIO;

	return len;
}

static const struct file_operations buzzer_fops = {
	.owner = THIS_MODULE,
	.write = buzzer_write,
};

static struct miscdevice buzzer_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mybuzzer",
	.fops = &buzzer_fops,
	.mode = 0600,
};

/* --- /dev/mycarctl : 1 字节掩码（车机本地 LED / 倒车蜂鸣） ------------------
 *
 *   写 1 字节 → 转发给 M0（SC_CMD_CARCTL），M0 在 idle 里执行：
 *                转向/双闪 1.5Hz 闪、近光 50% 亮度、远光 100%、倒车"滴滴-滴滴-滴滴"
 *   读 1 字节 → 返回 M0 最近一次回执的掩码（正常 ≤200 ms 就有）
 *
 * 掩码位：bit0 左转 bit1 右转 bit2 双闪 bit3 倒车 bit4 近光 bit5 远光
 * （与 docs/CAN_接口定义与操作_20260929.md 第 5.3 节一致）
 *
 * 注：三个状态变量 sc_car_mask / sc_car_echo / sc_car_echo_valid 声明在文件上方
 *     （sc_callback 里要用到它们，C 要求先声明后使用）。
 */
static ssize_t carctl_write(struct file *fp, const char __user *buf, size_t len,
			    loff_t *off)
{
	u8 mask;
	struct sc_frame f;
	int ret;

	if (len < 1)
		return -EINVAL;
	if (copy_from_user(&mask, buf, 1))
		return -EFAULT;

	mutex_lock(&sc_lock);
	sc_car_mask = mask;
	mutex_unlock(&sc_lock);

	memset(&f, 0, sizeof(f));
	f.cmd = SC_CMD_CARCTL;
	f.seq = 5;
	f.a = mask;

	/*
	 * ⚠️ 实测坑：M0 的 rpmsg 接收队列偶尔是满的（它正在采 SR04/DHT11），
	 * 单次 rpmsg_sendto 会返回非 0。早先版本直接 return -EIO，于是应用日志里
	 * 出现 "写 /dev/mycarctl 失败: Input/output error" —— 但 M0 那边其实收到了
	 * （回执能对上）。丢一次掩码的后果是转向灯卡在旧状态，所以这里**重试**，
	 * 并且即使最终没发出去也**不把 write() 判成失败**（真实状态以 M0 回执为准，
	 * 应用读 /dev/mycarctl 就能看到）。
	 *
	 * ★ 2026-10-13（F2）：重试必须用**非阻塞**发送。
	 * 原来这里是 `3 x sc_send()`（阻塞版），而阻塞版在 TX vring 没 buffer 时要等
	 * 满 500 ms 超时 —— 于是**一次 write() 可以在内核里卡 1.5 秒**，加上工作队列
	 * 里 DHT push 的那 500 ms，就是"板子没反应"。实测指纹：连续的
	 * `timeout waiting for a tx buffer` 之间会出现 ~1.5 s 的间隔，那正是这个循环。
	 * 改成非阻塞 + 6 x 200 µs 后，最坏代价约 1 ms，且失败也只是"这一帧没发出去"，
	 * 与原来"发不出去也不报错"的语义完全一致。
	 */
	ret = sc_send_retry(&f, 6);
	if (ret)
		dev_warn_ratelimited(&sc_rpdev->dev,
				     "carctl mask 0x%02x: rpmsg busy after retries (%d)\n",
				     mask, ret);

	return len;
}

static ssize_t carctl_read(struct file *fp, char __user *buf, size_t len,
			   loff_t *off)
{
	u8 v;

	if (len < 1)
		return -EINVAL;

	mutex_lock(&sc_lock);
	v = sc_car_echo_valid ? sc_car_echo : sc_car_mask;
	mutex_unlock(&sc_lock);

	if (copy_to_user(buf, &v, 1))
		return -EFAULT;

	return 1;
}

static const struct file_operations carctl_fops = {
	.owner = THIS_MODULE,
	.write = carctl_write,
	.read = carctl_read,
};

static struct miscdevice carctl_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mycarctl",
	.fops = &carctl_fops,
	.mode = 0600,
};

/* --- rpmsg driver -------------------------------------------------------- */
static int sc_probe(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "M0 AMP service found, subscribing\n");
	sc_rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, rpdev);

	/* SR04 every 100 ms; ask the M0 for the DHT11 only if we do not bit-bang it */
	sc_subscribe(100, dht11_local ? 0U : 2000U);

	return 0;
}

static void sc_remove(struct rpmsg_device *rpdev)
{
	sc_subscribe(0, 0);
	sc_rpdev = NULL;
	dev_info(&rpdev->dev, "M0 AMP service gone\n");
}

static struct rpmsg_device_id sc_id_table[] = {
	{ .name = SC_EPT_NAME },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, sc_id_table);

static struct rpmsg_driver sc_driver = {
	.drv.name = "sc_sensors",
	.id_table = sc_id_table,
	.probe = sc_probe,
	.callback = sc_callback,
	.remove = sc_remove,
};

static int __init sc_init(void)
{
	int ret;

	ret = misc_register(&sr04_misc);
	if (ret) {
		pr_err("sc_sensors: register mysr04 failed: %d\n", ret);
		return ret;
	}
	ret = misc_register(&dht11_misc);
	if (ret) {
		pr_err("sc_sensors: register mydht11 failed: %d\n", ret);
		goto unreg_sr04;
	}
	ret = misc_register(&buzzer_misc);
	if (ret) {
		pr_err("sc_sensors: register mybuzzer failed: %d\n", ret);
		goto unreg_dht;
	}
	ret = misc_register(&carctl_misc);
	if (ret) {
		pr_err("sc_sensors: register mycarctl failed: %d\n", ret);
		goto unreg_buzzer;
	}

	if (dht11_local) {
		ret = gpio_request(dht11_pin, "sc_dht11");
		if (ret) {
			pr_warn("sc_sensors: gpio %d busy/unavailable (%d), falling back to the M0 cache\n",
				dht11_pin, ret);
			dht11_local = 0;
		} else {
			int cfg, idle;

			gpio_direction_input(dht11_pin);
			/*
			 * Bias first (see sc_dht_pad_pull_up): the pad powers up
			 * pull-down and without a pull-up the DHT11 cannot drive
			 * the bus high at all.  The generic pinctrl request is
			 * kept as a fallback for other boards/wirings.
			 */
			cfg = sc_dht_pad_pull_up(dht11_pin);
			if (cfg)
				cfg = pinctrl_gpio_set_config(dht11_pin, PIN_CONFIG_BIAS_PULL_UP);
			dht11_gpio = dht11_pin;
			idle = gpio_get_value(dht11_pin) ? 1 : 0;
			pr_info("sc_sensors: DHT11 bit-banged on the A7, gpio %d, pull-up %s, idle level %d%s\n",
				dht11_pin, cfg ? "hint not applicable" : "applied", idle,
				idle ? "" : " - BUS LOW: check sensor power / wiring");
		}
	}

	ret = register_rpmsg_driver(&sc_driver);
	if (ret) {
		pr_err("sc_sensors: register rpmsg driver failed: %d\n", ret);
		goto unreg_gpio;
	}

	INIT_DELAYED_WORK(&sc_dht_work, sc_dht_poll_work);
	if (dht11_local && dht11_poll_ms > 0)
		schedule_delayed_work(&sc_dht_work, msecs_to_jiffies(dht11_poll_ms));

	pr_info("sc_sensors: /dev/mysr04 /dev/mybuzzer from the M0, /dev/mydht11 %s, self-poll %d ms\n",
		dht11_local ? "bit-banged on the A7" : "from the M0",
		dht11_local ? dht11_poll_ms : 0);

	return 0;

unreg_buzzer:
	misc_deregister(&carctl_misc);
unreg_gpio:
	if (dht11_gpio >= 0)
		gpio_free(dht11_gpio);
	misc_deregister(&buzzer_misc);
unreg_dht:
	misc_deregister(&dht11_misc);
unreg_sr04:
	misc_deregister(&sr04_misc);

	return ret;
}

static void __exit sc_exit(void)
{
	cancel_delayed_work_sync(&sc_dht_work);
	unregister_rpmsg_driver(&sc_driver);
	if (dht11_gpio >= 0)
		gpio_free(dht11_gpio);
	misc_deregister(&carctl_misc);
	misc_deregister(&buzzer_misc);
	misc_deregister(&dht11_misc);
	misc_deregister(&sr04_misc);
	pr_info("sc_sensors: unloaded (rpmsg rx %u samples, dht11 fails %u)\n",
		sc_rx_count, dht11_fail);
}

module_init(sc_init);
module_exit(sc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK3506 AMP split: M0-sampled SR04/buzzer + A7-bitbanged DHT11");
