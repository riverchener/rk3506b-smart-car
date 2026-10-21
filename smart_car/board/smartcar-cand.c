/*
 * smartcar-cand —— 车机 CAN 守护进程（独立于 GUI，能收能发、可无硬件自测）
 * ============================================================================
 * 为什么还要一个守护进程（app 里已经有 CanMgr 了）：
 *   · 车机界面没跑（崩溃排查、只开了 ssh）时，总线日志不能断 —— 现场排障全靠它；
 *   · 需要一个"不依赖电脑、不依赖 CAN 分析仪"的自测入口：
 *       --selftest 用 vcan0 自己发自己收，验证内核 SocketCAN 通路与协议编解码；
 *       --sim      按开车时间线往 vcan0 灌帧，车机界面就能在**没有任何硬件**时动起来；
 *   · 车机 app 与守护进程**同时**绑 can0 是允许的（SocketCAN 多读者各拿一份），
 *     所以两者并存、互不影响。
 *
 * 用法：
 *   smartcar-cand -i can0                前台运行，总线上所有帧记进 /var/log/smartcar-can.log
 *   smartcar-cand -d                     后台守护（pidfile /var/run/smartcar-cand.pid）
 *   smartcar-cand --selftest             无硬件自测：建 vcan0 → 发 4 帧 → 校验收到且解码正确
 *   smartcar-cand --sim 30 -i vcan0      往 vcan0 灌 30 秒开车时间线（界面会跟着动）
 *   smartcar-cand --check                只跑内置协议编解码用例（不需要任何 CAN 设备，主机上也能跑）
 *
 * 日志格式（一行一帧，便于 grep/awk）：
 *   09-29 20:31:02.123 can0 RX 100#4201030000000000   车速=66km/h D档 左转
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define LOG_DEFAULT "/var/log/smartcar-can.log"
#define LOG_MAX_BYTES (2 * 1024 * 1024)

static volatile int g_stop = 0;
static int g_quiet = 0;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ---------------------------------------------------------------- 小工具 */
static double now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void stamp(char *buf, size_t n)
{
    struct timeval tv;
    struct tm tm;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    snprintf(buf, n, "%02d-%02d %02d:%02d:%02d.%03d",
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             (int)(tv.tv_usec / 1000));
}

/* ---------------------------------------------------------------- 协议解码 */
/* 与 docs/CAN_接口定义与操作_20260929.md 逐位一致；只做展示，不参与控制逻辑 */
static void decode(unsigned id, const unsigned char *d, int dlc, char *out, size_t n)
{
    out[0] = '\0';
    if (id == 0x100 && dlc >= 4) {
        static const char *gear[4] = { "P", "R", "N", "D" };
        char flags[96] = "";
        const unsigned char b = d[1];
        if (b & 0x01) strncat(flags, "左转 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x02) strncat(flags, "右转 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x04) strncat(flags, "刹车 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x08) strncat(flags, "倒车 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x10) strncat(flags, "安全带未系 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x20) strncat(flags, "手刹 ", sizeof(flags) - strlen(flags) - 1);
        if (b & 0x40) strncat(flags, "双闪 ", sizeof(flags) - strlen(flags) - 1);
        const char *g = d[2] < 4 ? gear[d[2]] : "?";
        if (d[0] == 0xFF)
            snprintf(out, n, "车速=无效 %s档 %urpm %s", g, d[3] * 100, flags);
        else
            snprintf(out, n, "车速=%ukm/h %s档 %urpm %s", d[0], g, d[3] * 100, flags);
    } else if (id == 0x101 && dlc >= 2) {
        static const char *door[6] = { "左前", "右前", "左后", "右后门", "后备箱", "引擎盖" };
        char op[96] = "";
        for (int i = 0; i < 6; ++i)
            if (d[0] & (1u << i)) {
                strncat(op, door[i], sizeof(op) - strlen(op) - 1);
                strncat(op, "开 ", sizeof(op) - strlen(op) - 1);
            }
        snprintf(out, n, "%s%s", op[0] ? op : "门全关 ", (d[1] & 1) ? "已锁" : "未上锁");
    } else if (id == 0x102 && dlc >= 4) {
        static const char *lt[6] = { "近光", "远光", "示宽", "前雾", "后雾", "氛围灯" };
        char on[64] = "";
        for (int i = 0; i < 6; ++i)
            if (d[0] & (1u << i)) {
                strncat(on, lt[i], sizeof(on) - strlen(on) - 1);
                strncat(on, "/", sizeof(on) - strlen(on) - 1);
            }
        snprintf(out, n, "灯[%s] %s %u℃ 车外%d℃", on[0] ? on : "-",
                 (d[1] & 1) ? "AC开" : "AC关", d[2], d[3] ? (int)d[3] - 40 : 0);
    } else if (id == 0x201 && dlc >= 4) {
        snprintf(out, n, "空调%s %u℃ 风量%u %s", d[0] ? "开" : "关", d[1], d[2],
                 (d[3] & 1) ? "内循环" : "外循环");
    } else if (id == 0x202 && dlc >= 3) {
        static const char *w[5] = { "全部", "左前", "右前", "左后", "右后" };
        static const char *a[3] = { "停", "升", "降" };
        snprintf(out, n, "车窗%s %s %u%%", d[0] < 5 ? w[d[0]] : "?", d[1] < 3 ? a[d[1]] : "?",
                 d[2]);
    } else if (id == 0x203 && dlc >= 2) {
        snprintf(out, n, "灯光开掩码0x%02X 关掩码0x%02X", d[0], d[1]);
    }
}

static void frame_to_hex(unsigned id, const unsigned char *d, int dlc, char *out, size_t n)
{
    int k = snprintf(out, n, "%03X#", id);
    for (int i = 0; i < dlc && k < (int)n - 3; ++i)
        k += snprintf(out + k, n - k, "%02X", d[i]);
}

/* ---------------------------------------------------------------- 日志 */
static FILE *open_log(const char *path)
{
    if (!path || !*path)
        return NULL;
    FILE *f = fopen(path, "a");
    if (!f)
        return NULL;
    /* 超过上限就清空重来（现场日志不需要历史归档，只要最近一段） */
    fseek(f, 0, SEEK_END);
    if (ftell(f) > LOG_MAX_BYTES) {
        fclose(f);
        f = fopen(path, "w");
    }
    return f;
}

static void log_frame(FILE *f, const char *iface, const char *dir, unsigned id,
                      const unsigned char *d, int dlc, const char *sim)
{
    if (!f)
        return;
    char ts[48], hex[32], dec[192];
    stamp(ts, sizeof(ts));
    frame_to_hex(id, d, dlc, hex, sizeof(hex));
    decode(id, d, dlc, dec, sizeof(dec));
    fprintf(f, "%s %s %s %s%s%s\n", ts, iface, dir, hex,
            dec[0] ? "  " : "", dec);
    fflush(f);
    if (!g_quiet && !sim)
        printf("%s %s %s %s%s%s\n", ts, iface, dir, hex, dec[0] ? "  " : "", dec);
    else if (!g_quiet && sim)
        printf("  TX %s  %s\n", hex, dec);
}

/* ---------------------------------------------------------------- SocketCAN */
static int open_can(const char *iface)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "接口 %s 不存在: %s\n", iface, strerror(errno));
        close(fd);
        return -1;
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    return fd;
}

static int send_frame(int fd, unsigned id, const unsigned char *d, int dlc)
{
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id = id & CAN_SFF_MASK;
    f.can_dlc = dlc;
    memcpy(f.data, d, dlc);
    return write(fd, &f, sizeof(f)) == (ssize_t)sizeof(f) ? 0 : -1;
}

/* 让 socket 也能收到自己发的帧。
 * ⚠️ 实测（VM 内核 vcan0）：vcan 只把帧投给**其它** socket，发送者自己收不到 ——
 *    所以 `--sim 灌帧 + cantool dump` 能收到，而"自发自收"的自测收不到。
 *    打开 CAN_RAW_RECV_OWN_MSGS 后自带回读才能工作（真硬件上的回环也是这个语义）。
 *    守护进程常驻收帧时不打开，免得把自己的 TX 又当 RX 记一遍。 */
static void set_recv_own_msgs(int fd, int on)
{
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &on, sizeof(on));
}

/* ---------------------------------------------------------------- 内置用例 */
/* 不需要 CAN 设备：校验编解码与手册一致（主机上也能跑，CI 友好） */
static int run_check(void)
{
    struct { unsigned id; unsigned char d[8]; int dlc; const char *want; } cases[] = {
        { 0x100, { 66, 0x01, 3, 8, 0, 0, 0, 0 }, 8, "车速=66km/h D档 800rpm 左转 " },
        { 0x100, { 0, 0x08, 1, 0, 0, 0, 0, 0 }, 8, "车速=0km/h R档 0rpm 倒车 " },
        { 0x100, { 0xFF, 0, 0, 0, 0, 0, 0, 0 }, 8, "车速=无效 P档 0rpm " },
        { 0x101, { 0x01, 0, 0, 0, 0, 0, 0, 0 }, 8, "左前开 未上锁" },
        { 0x102, { 0x01, 1, 24, 60, 0, 0, 0, 0 }, 8, "灯[近光/] AC开 24℃ 车外20℃" },
        { 0x201, { 1, 24, 3, 1, 0, 0, 0, 0 }, 8, "空调开 24℃ 风量3 内循环" },
        { 0x202, { 1, 1, 100, 0, 0, 0, 0, 0 }, 8, "车窗左前 升 100%" },
        { 0x203, { 0x01, 0x02, 0, 0, 0, 0, 0, 0 }, 8, "灯光开掩码0x01 关掩码0x02" },
    };
    int bad = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char got[192];
        decode(cases[i].id, cases[i].d, cases[i].dlc, got, sizeof(got));
        const int ok = strcmp(got, cases[i].want) == 0;
        if (!ok)
            ++bad;
        printf("  %s 0x%03X -> \"%s\"%s%s\n", ok ? "✓" : "✗", cases[i].id, got,
               ok ? "" : "  期望: ", ok ? "" : cases[i].want);
    }
    printf("内置用例：%s\n", bad ? "有失败" : "全部通过");
    return bad ? 1 : 0;
}

/* ---------------------------------------------------------------- 自测 */
/* 建 vcan0 → 发 4 帧 → 读回并核对：验证内核 SocketCAN 通路 + 编解码（不需要任何硬件） */
static int run_selftest(const char *iface_in)
{
    const char *iface = iface_in && *iface_in ? iface_in : "vcan0";
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link show %s >/dev/null 2>&1 || ip link add dev %s type vcan", iface, iface);
    if (system(cmd) != 0)
        fprintf(stderr, "提示：创建 %s 失败（内核要有 CONFIG_CAN_VCAN=y）\n", iface);
    snprintf(cmd, sizeof(cmd), "ip link set %s up", iface);
    if (system(cmd) != 0) {
        /* 失败不一定是真失败：接口可能本来就 up 了（非 root 跑自测时会这样）。
         * 板上守护进程是 root，这里只是让它幂等、别误报。 */
        char q[256];
        snprintf(q, sizeof(q), "ip link show %s 2>/dev/null | grep -q ',UP'", iface);
        if (system(q) != 0) {
            fprintf(stderr, "拉起 %s 失败（需要 root 或接口不存在）\n", iface);
            return 1;
        }
    }
    int fd = open_can(iface);
    if (fd < 0)
        return 1;
    set_recv_own_msgs(fd, 1);          /* vcan 不回给自己，见上面注释 */

    struct { unsigned id; unsigned char d[8]; const char *want; } t[] = {
        { 0x100, { 66, 0x01, 3, 8, 0, 0, 0, 0 }, "车速=66km/h D档 800rpm 左转 " },
        { 0x101, { 0x01, 0, 0, 0, 0, 0, 0, 0 }, "左前开 未上锁" },
        { 0x102, { 0x01, 1, 24, 60, 0, 0, 0, 0 }, "灯[近光/] AC开 24℃ 车外20℃" },
        { 0x203, { 0x01, 0x02, 0, 0, 0, 0, 0, 0 }, "灯光开掩码0x01 关掩码0x02" },
    };
    printf("== 自测：接口 %s，发 %u 帧并读回 ==\n", iface, (unsigned)(sizeof(t) / sizeof(t[0])));
    int bad = 0;
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); ++i) {
        if (send_frame(fd, t[i].id, t[i].d, 8) != 0) {
            printf("  ✗ 0x%03X 发送失败: %s\n", t[i].id, strerror(errno));
            ++bad;
            continue;
        }
        usleep(50000);
        struct can_frame f;
        int got = 0;
        const double end = now_s() + 1.0;
        while (now_s() < end) {
            const ssize_t n = read(fd, &f, sizeof(f));
            if (n == (ssize_t)sizeof(f) && (f.can_id & CAN_SFF_MASK) == t[i].id) {
                char dec[192];
                decode(f.can_id & CAN_SFF_MASK, f.data, f.can_dlc, dec, sizeof(dec));
                const int ok = strcmp(dec, t[i].want) == 0;
                printf("  %s 0x%03X 回读 %s\n", ok ? "✓" : "✗", t[i].id, dec);
                if (!ok)
                    ++bad;
                got = 1;
                break;
            }
            usleep(20000);
        }
        if (!got) {
            printf("  ✗ 0x%03X 1 秒内没读回自己的帧（回环没开？接口没 up？）\n", t[i].id);
            ++bad;
        }
    }
    close(fd);
    printf("自测结果：%s\n", bad ? "有失败" : "全部通过（收发通路 + 编解码都对）");
    return bad ? 1 : 0;
}

/* ---------------------------------------------------------------- 灌帧 */
/* 按开车时间线往接口灌帧（车机界面在没有任何硬件时也能动起来） */
static int run_sim(int fd, const char *iface, FILE *log, int seconds)
{
    printf("== 模拟开车：往 %s 灌帧 %d 秒（车机界面应跟着变化）==\n", iface, seconds);
    const double t0 = now_s();
    int tick = 0;
    while (!g_stop) {
        const double el = now_s() - t0;
        if (seconds > 0 && el >= seconds)
            break;
        const int t = tick % 260;                     /* 26 秒一轮，和 app 内置演示同节拍 */
        int speed = 0;
        if (t < 10) speed = 0;
        else if (t < 70) speed = (t - 10) * 66 / 60;
        else if (t < 200) speed = 66;
        else speed = 66 - (t - 200) * 66 / 40;
        if (speed < 0) speed = 0;

        unsigned char d100[8] = { (unsigned char)speed, 0, (t >= 200) ? 1 : 3, 8, 0, 0, 0, 0 };
        if (t >= 80 && t < 110) d100[1] |= 0x01;
        if (t >= 110 && t < 140) d100[1] |= 0x04;
        if (t >= 140 && t < 200) d100[1] |= 0x10;
        if (t >= 170 && t < 200) d100[1] |= 0x02;
        if (t >= 200) d100[1] |= 0x08;
        unsigned char d101[8] = { (t >= 170 && t < 200) ? 1 : 0, 0, 0, 0, 0, 0, 0, 0 };
        unsigned char d102[8] = { (t >= 110 && t < 200) ? 1 : 0, 1, 24, 60, 0, 0, 0, 0 };

        send_frame(fd, 0x100, d100, 8);
        send_frame(fd, 0x101, d101, 8);
        send_frame(fd, 0x102, d102, 8);
        if (tick % 10 == 0)
            log_frame(log, iface, "TX", 0x100, d100, 8, "sim");

        /* 顺便把总线上别人的帧（车机发的 0x201/0x202/0x203）也记下来 */
        struct can_frame f;
        while (read(fd, &f, sizeof(f)) == (ssize_t)sizeof(f))
            log_frame(log, iface, "RX", f.can_id & CAN_SFF_MASK, f.data, f.can_dlc, NULL);

        ++tick;
        usleep(100000);
    }
    return 0;
}

/* ---------------------------------------------------------------- main */
static void usage(void)
{
    fprintf(stderr,
            "用法: smartcar-cand [选项]\n"
            "  -i <iface>    接口，默认 can0（不存在时自动试 vcan0）\n"
            "  -l <file>     日志文件，默认 " LOG_DEFAULT "（空字符串=不记）\n"
            "  -d            后台运行（pidfile /var/run/smartcar-cand.pid）\n"
            "  -q            安静（不打印到 stdout，只写日志）\n"
            "  --selftest    无硬件自测（vcan0 自发自收 + 编解码核对）\n"
            "  --check       只跑内置编解码用例（不需要 CAN 设备）\n"
            "  --sim <秒>    往接口灌开车时间线（配合 vcan0 可无硬件演示）\n");
}

int main(int argc, char **argv)
{
    const char *iface = NULL, *logpath = LOG_DEFAULT;
    int daemonize = 0, sim_s = 0, selftest = 0, check = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) iface = argv[++i];
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) logpath = argv[++i];
        else if (!strcmp(argv[i], "-d")) daemonize = 1;
        else if (!strcmp(argv[i], "-q")) g_quiet = 1;
        else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--sim")) sim_s = (i + 1 < argc) ? atoi(argv[++i]) : 30;
        else { usage(); return 2; }
    }

    if (check)
        return run_check();
    if (selftest)
        return run_selftest(iface);

    if (daemonize) {
        if (daemon(0, 0) != 0) {
            perror("daemon");
            return 1;
        }
        FILE *pf = fopen("/var/run/smartcar-cand.pid", "w");
        if (pf) {
            fprintf(pf, "%d\n", (int)getpid());
            fclose(pf);
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 默认 can0；不在就退到 vcan0，两者都不在才算失败 */
    char chosen[IFNAMSIZ] = "";
    const char *try_list[3] = { iface, "can0", "vcan0" };
    int fd = -1;
    for (int i = 0; i < 3 && fd < 0; ++i) {
        if (!try_list[i] || !*try_list[i])
            continue;
        fd = open_can(try_list[i]);
        if (fd >= 0)
            snprintf(chosen, sizeof(chosen), "%s", try_list[i]);
    }
    if (fd < 0) {
        fprintf(stderr, "打不开任何 CAN 接口（can0/vcan0）—— 内核没编 CAN？见 /etc/init.d/S49can\n");
        return 1;
    }

    FILE *log = open_log(logpath);
    printf("smartcar-cand: 接口 %s，日志 %s（%s）\n", chosen,
           log ? logpath : "(不记)", daemonize ? "后台" : "前台");

    if (sim_s > 0) {
        const int rc = run_sim(fd, chosen, log, sim_s);
        close(fd);
        if (log) fclose(log);
        return rc;
    }

    /* 常驻：收帧 → 解码 → 记日志（GUI 没跑时总线也不丢） */
    while (!g_stop) {
        struct can_frame f;
        int n = 0;
        while (read(fd, &f, sizeof(f)) == (ssize_t)sizeof(f)) {
            if (f.can_id & CAN_ERR_FLAG) {
                char ts[48];
                stamp(ts, sizeof(ts));
                if (log) {
                    fprintf(log, "%s %s ERR id=0x%08X\n", ts, chosen, f.can_id);
                    fflush(log);
                }
                continue;
            }
            log_frame(log, chosen, "RX", f.can_id & CAN_SFF_MASK, f.data, f.can_dlc, NULL);
            ++n;
        }
        if (n == 0)
            usleep(50000);
    }
    printf("smartcar-cand: 退出\n");
    close(fd);
    if (log)
        fclose(log);
    return 0;
}
