/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sc_client - Linux (A7) userspace client for the RK3506B AMP service.
 *
 * Uses the RPMsg char interface only (no vendor test driver):
 *   /dev/rpmsg_ctrl0 + RPMSG_CREATE_EPT_IOCTL -> /dev/rpmsgN, then raw
 *   read/write of fixed 24-byte frames.
 *
 * Commands:
 *   sc_client info
 *   sc_client ping [count]                 round-trip latency statistics
 *   sc_client time                         M0 timestamp
 *   sc_client jitter [iter] [period_us]    M0-side scheduling jitter
 *   sc_client sr04 [timeout_us]            ultrasonic distance (M0 timed)
 *   sc_client dht11                        temperature / humidity
 *   sc_client buzzer <freq_hz> <ms>
 *   sc_client loop [pulses]                TRIG->ECHO self test
 *   sc_client stress <count>               back-to-back round trips
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>

#define RPMSG_ADDR_ANY 0xFFFFFFFFU

struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

#define SC_EPT_ID    0x4004U
#define SC_EPT_NAME  "rpmsg-smartcar"

#define SC_RSP        0x8000U
#define SC_CMD_PING   0x0001U
#define SC_CMD_TIME   0x0002U
#define SC_CMD_JITTER 0x0003U
#define SC_CMD_BUZZER 0x0004U
#define SC_CMD_SR04   0x0005U
#define SC_CMD_DHT11  0x0006U
#define SC_CMD_LOOP   0x0007U
#define SC_CMD_INFO   0x0008U
#define SC_CMD_SUBSCRIBE 0x000AU
#define SC_CMD_LATEST 0x000BU
#define SC_CMD_TONE   0x000CU
#define SC_EVT_SAMPLE 0x8081U
#define SC_EVT_SR04_VALID 0x01U
#define SC_EVT_DHT_VALID  0x02U

struct sc_frame {
    uint16_t cmd;
    uint16_t seq;
    uint32_t status;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t t;
} __attribute__((packed));

static const char *st_name(uint32_t s)
{
    switch (s) {
    case 0: return "ok";
    case 1: return "timeout";
    case 2: return "no-device";
    case 3: return "checksum";
    case 4: return "bad-arg";
    default: return "?";
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int find_dev(char *out, size_t out_sz)
{
    DIR *d = opendir("/sys/class/rpmsg");
    struct dirent *e;
    int found = 0;

    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[256], name[64];
        FILE *f;

        if (strncmp(e->d_name, "rpmsg", 5) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/name", e->d_name);
        f = fopen(path, "r");
        if (!f) {
            continue;
        }
        if (fgets(name, sizeof(name), f)) {
            name[strcspn(name, "\n")] = 0;
            if (strcmp(name, SC_EPT_NAME) == 0) {
                snprintf(out, out_sz, "/dev/%s", e->d_name);
                found = 1;
            }
        }
        fclose(f);
    }
    closedir(d);

    return found ? 0 : -1;
}

/* returns fd, or -1 */
static int open_ept(void)
{
    struct rpmsg_endpoint_info info;
    char dev[128];
    int cfd, fd;

    cfd = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (cfd < 0) {
        fprintf(stderr, "open /dev/rpmsg_ctrl0: %s\n", strerror(errno));
        return -1;
    }

    memset(&info, 0, sizeof(info));
    strncpy(info.name, SC_EPT_NAME, sizeof(info.name) - 1);
    info.src = RPMSG_ADDR_ANY;
    info.dst = SC_EPT_ID;

    if (ioctl(cfd, RPMSG_CREATE_EPT_IOCTL, &info) < 0) {
        fprintf(stderr, "RPMSG_CREATE_EPT_IOCTL: %s\n", strerror(errno));
        close(cfd);
        return -1;
    }
    close(cfd);

    /* the node appears asynchronously under /sys/class/rpmsg */
    for (int i = 0; i < 50; i++) {
        if (find_dev(dev, sizeof(dev)) == 0) {
            fd = open(dev, O_RDWR);
            if (fd >= 0) {
                return fd;
            }
        }
        usleep(100000);
    }
    fprintf(stderr, "created endpoint but no /dev node for '%s'\n", SC_EPT_NAME);

    return -1;
}

static int sc_xfer(int fd, struct sc_frame *req, struct sc_frame *rsp, int timeout_ms)
{
    struct pollfd pfd;
    ssize_t n;
    int rc;

    if (write(fd, req, sizeof(*req)) != (ssize_t)sizeof(*req)) {
        fprintf(stderr, "write: %s\n", strerror(errno));
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) {
        return -1;
    }

    n = read(fd, rsp, sizeof(*rsp));
    if (n != (ssize_t)sizeof(*rsp)) {
        fprintf(stderr, "read %zd: %s\n", n, strerror(errno));
        return -1;
    }
    if ((rsp->cmd & 0xFF00U) != SC_RSP || rsp->seq != req->seq) {
        fprintf(stderr, "unexpected reply cmd=0x%x seq=%u\n", rsp->cmd, rsp->seq);
        return -1;
    }

    return 0;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

    return (x > y) - (x < y);
}

static int cmd_ping(int fd, int count)
{
    uint64_t *v;
    uint64_t sum = 0;
    int i, ok = 0;

    if (count <= 0) {
        count = 200;
    }
    v = calloc(count, sizeof(*v));
    if (!v) {
        return 1;
    }

    for (i = 0; i < count; i++) {
        struct sc_frame req, rsp;
        uint64_t t0, t1;

        memset(&req, 0, sizeof(req));
        req.cmd = SC_CMD_PING;
        req.seq = (uint16_t)(i & 0xFFFF);

        t0 = now_ns();
        if (sc_xfer(fd, &req, &rsp, 1000) == 0) {
            t1 = now_ns();
            v[ok++] = t1 - t0;
            sum += t1 - t0;
        }
        usleep(500);
    }

    if (ok == 0) {
        printf("ping: no replies\n");
        free(v);
        return 1;
    }

    qsort(v, ok, sizeof(*v), cmp_u64);
    printf("ping: %d/%d replies\n", ok, count);
    printf("  min    %8.3f us\n", v[0] / 1000.0);
    printf("  p50    %8.3f us\n", v[ok / 2] / 1000.0);
    printf("  p90    %8.3f us\n", v[(int)(ok * 0.90)] / 1000.0);
    printf("  p99    %8.3f us\n", v[(int)(ok * 0.99)] / 1000.0);
    printf("  max    %8.3f us\n", v[ok - 1] / 1000.0);
    printf("  avg    %8.3f us\n", (double)sum / ok / 1000.0);
    printf("  jitter %8.3f us (max-min)\n", (v[ok - 1] - v[0]) / 1000.0);
    free(v);

    return 0;
}

/*
 * monitor: subscribe to the M0's periodic sampling and print every pushed
 * sample together with the arrival interval, so the cadence is visible.
 */
static int cmd_monitor(int fd, int seconds, int sr04_ms, int dht_ms)
{
    struct sc_frame req, rsp;
    uint64_t t_start, t_prev = 0;
    int n = 0;

    if (seconds <= 0) {
        seconds = 10;
    }

    memset(&req, 0, sizeof(req));
    req.cmd = SC_CMD_SUBSCRIBE;
    req.seq = 1;
    req.a = (uint32_t)(sr04_ms < 0 ? 0 : sr04_ms);
    req.b = (uint32_t)(dht_ms < 0 ? 0 : dht_ms);

    if (sc_xfer(fd, &req, &rsp, 5000) != 0) {
        fprintf(stderr, "subscribe failed\n");

        return 1;
    }
    printf("subscribed: sr04 every %d ms, dht11 every %d ms (0 = off)\n", sr04_ms, dht_ms);
    printf("cached now: sr04=%s%u us / %u mm  dht11=%s%.1f C / %.1f %%\n",
           (rsp.status & SC_EVT_SR04_VALID) ? "" : "invalid ",
           rsp.a, rsp.b,
           (rsp.status & SC_EVT_DHT_VALID) ? "" : "invalid ",
           (rsp.c >> 16) / 10.0, (rsp.c & 0xFFFFU) / 10.0);
    printf("  time(s)     dt(ms)   echo(us)   dist(mm)   temp(C)  hum(%%)  m0_t(us)\n");

    t_start = now_ns();
    while ((now_ns() - t_start) < (uint64_t)seconds * 1000000000ULL) {
        struct pollfd pfd;
        ssize_t got;
        uint64_t t_now;

        pfd.fd = fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 500) <= 0) {
            continue;
        }
        got = read(fd, &rsp, sizeof(rsp));
        if (got != (ssize_t)sizeof(rsp)) {
            continue;
        }
        if (rsp.cmd != SC_EVT_SAMPLE) {
            printf("  (other frame cmd=0x%x status=%u)\n", rsp.cmd, rsp.status);
            continue;
        }

        t_now = now_ns();
        printf("  %8.3f  %8.3f   %8u  %8u   %6.1f  %6.1f  %10u%s\n",
               (t_now - t_start) / 1e9,
               t_prev ? (t_now - t_prev) / 1e6 : 0.0,
               rsp.a, rsp.b,
               (rsp.c >> 16) / 10.0, (rsp.c & 0xFFFFU) / 10.0, rsp.t,
               (rsp.status & SC_EVT_SR04_VALID) ? "" :
               ((rsp.status & SC_EVT_DHT_VALID) ? "  (sr04 invalid)" : "  (both invalid)"));
        t_prev = t_now;
        n++;
    }

    /* stop the periodic work again */
    memset(&req, 0, sizeof(req));
    req.cmd = SC_CMD_SUBSCRIBE;
    req.seq = 2;
    sc_xfer(fd, &req, &rsp, 5000);
    printf("monitor: %d samples\n", n);

    return n ? 0 : 1;
}

static int cmd_stress(int fd, int count)
{
    struct sc_frame req, rsp;
    uint64_t t0, t1;
    int i, ok = 0;

    if (count <= 0) {
        count = 5000;
    }
    t0 = now_ns();
    for (i = 0; i < count; i++) {
        memset(&req, 0, sizeof(req));
        req.cmd = SC_CMD_PING;
        req.seq = (uint16_t)i;
        if (sc_xfer(fd, &req, &rsp, 1000) == 0) {
            ok++;
        }
    }
    t1 = now_ns();

    printf("stress: %d/%d round trips in %.3f s -> %.1f trips/s, avg %.3f us\n",
           ok, count, (t1 - t0) / 1e9, ok / ((t1 - t0) / 1e9),
           ok ? (double)(t1 - t0) / ok / 1000.0 : 0.0);

    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    struct sc_frame req, rsp;
    const char *cmd;
    int fd, rc = 0;

    if (argc < 2) {
        printf("usage: %s {info|ping [n]|stress [n]|time|jitter [iter] [us]|"
               "sr04 [timeout_us]|dht11|tone <hz>|"
               "latest|monitor [sec] [sr04_ms] [dht_ms]}\n", argv[0]);
        return 2;
    }
    cmd = argv[1];

    fd = open_ept();
    if (fd < 0) {
        return 1;
    }

    /* ping/stress drive their own loop */
    if (strcmp(cmd, "ping") == 0) {
        rc = cmd_ping(fd, argc > 2 ? atoi(argv[2]) : 200);
        close(fd);

        return rc;
    }
    if (strcmp(cmd, "stress") == 0) {
        rc = cmd_stress(fd, argc > 2 ? atoi(argv[2]) : 5000);
        close(fd);

        return rc;
    }
    if (strcmp(cmd, "monitor") == 0) {
        rc = cmd_monitor(fd, argc > 2 ? atoi(argv[2]) : 10,
                         argc > 3 ? atoi(argv[3]) : 100,
                         argc > 4 ? atoi(argv[4]) : 2000);
        close(fd);

        return rc;
    }

    memset(&req, 0, sizeof(req));
    req.seq = 1;

    if (strcmp(cmd, "info") == 0) {
        req.cmd = SC_CMD_INFO;
    } else if (strcmp(cmd, "time") == 0) {
        req.cmd = SC_CMD_TIME;
    } else if (strcmp(cmd, "jitter") == 0) {
        req.cmd = SC_CMD_JITTER;
        req.a = argc > 2 ? (uint32_t)atoi(argv[2]) : 1000;
        req.b = argc > 3 ? (uint32_t)atoi(argv[3]) : 100;
    } else if (strcmp(cmd, "sr04") == 0) {
        req.cmd = SC_CMD_SR04;
        req.a = argc > 2 ? (uint32_t)atoi(argv[2]) : 30000;
    } else if (strcmp(cmd, "dht11") == 0) {
        req.cmd = SC_CMD_DHT11;
    } else if (strcmp(cmd, "tone") == 0) {
        req.cmd = SC_CMD_TONE;
        req.a = argc > 2 ? (uint32_t)atoi(argv[2]) : 0;
    } else if (strcmp(cmd, "latest") == 0) {
        req.cmd = SC_CMD_LATEST;
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        close(fd);

        return 2;
    }

    if (sc_xfer(fd, &req, &rsp, 15000) != 0) {
        fprintf(stderr, "no reply from the M0 service\n");
        close(fd);

        return 1;
    }

    switch (req.cmd) {
    case SC_CMD_INFO:
        printf("info  : fw=0x%08x  heap=%u bytes  timer=%u counts/us\n",
               rsp.a, rsp.b, rsp.c);
        break;
    case SC_CMD_TIME:
        printf("time  : M0 raw timer = 0x%08x%08x (%llu)\n", rsp.b, rsp.a,
               ((unsigned long long)rsp.b << 32) | rsp.a);
        break;
    case SC_CMD_JITTER:
        printf("jitter: min=%u us  max=%u us  sum=%u us  avg=%.1f us  (spread %u us)\n",
               rsp.a, rsp.b, rsp.c,
               ((req.a ? req.a : 1000) ? (double)rsp.c / (req.a ? req.a : 1000) : 0.0),
               rsp.b - rsp.a);
        break;
    case SC_CMD_SR04:
        if (rsp.status == 0) {
            printf("sr04  : echo=%u us -> %u mm  (%.1f cm)\n", rsp.a, rsp.b, rsp.b / 10.0);
        } else {
            printf("sr04  : %s (echo=%u us)\n", st_name(rsp.status), rsp.a);
        }
        break;
    case SC_CMD_DHT11:
        if (rsp.status == 0) {
            printf("dht11 : temp=%.1f C  hum=%.1f %%\n", rsp.a / 10.0, rsp.b / 10.0);
        } else {
            printf("dht11 : %s\n", st_name(rsp.status));
        }
        break;
    case SC_CMD_TONE:
        printf("tone  : buzzer %s (%u Hz)\n", rsp.a ? "on" : "off", rsp.a);
        break;
    case SC_CMD_LATEST:
    case SC_CMD_SUBSCRIBE:
        printf("latest: sr04=%s%u us / %u mm  dht11=%s%.1f C / %.1f %%\n",
               (rsp.status & SC_EVT_SR04_VALID) ? "" : "invalid ",
               rsp.a, rsp.b,
               (rsp.status & SC_EVT_DHT_VALID) ? "" : "invalid ",
               (rsp.c >> 16) / 10.0, (rsp.c & 0xFFFFU) / 10.0);
        break;
    default:
        printf("reply : cmd=0x%x status=%u a=%u b=%u c=%u t=%u\n",
               rsp.cmd, rsp.status, rsp.a, rsp.b, rsp.c, rsp.t);
        break;
    }

    close(fd);

    return rc;
}
