/*
 * cantool —— 板端 CAN 工具（不依赖 can-utils，本镜像里没装）
 * ============================================================================
 * 用法：
 *   cantool dump -i can0                     持续打印收到的帧：<id>#<data>
 *   cantool send -i can0 100 42 00 03 00 00 00 00 00
 *                                            ID 是十六进制，后面跟最多 8 个十六进制字节
 *   cantool loopback-test -i can0             内部回环自测（需先 ip link set can0 type can loopback on）
 *   cantool stats -i can0                     打印接口状态与计数
 *
 * 为什么要自己写：本 rootfs 没有 can-utils（apt 也装不了），而 Qt 侧只负责业务，
 * 现场排障需要一个"能收能发"的最小工具 —— 就是它。
 *
 * 编译（在 VM 上用 SDK 的交叉工具链）：
 *   $HOST/arm-buildroot-linux-gnueabihf-gcc -O2 -o cantool cantool.c
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <linux/can.h>
#include <linux/can/raw.h>

static volatile int g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

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
    return fd;
}

static void print_frame(const struct can_frame *f)
{
    printf("%03X#", (unsigned)(f->can_id & CAN_SFF_MASK));
    for (int i = 0; i < f->can_dlc; ++i)
        printf("%02X", f->data[i]);
    printf("\n");
    fflush(stdout);
}

static int hexbyte(const char *s, unsigned char *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 16);
    if (end == s || v < 0 || v > 0xff)
        return -1;
    *out = (unsigned char)v;
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "用法:\n"
            "  cantool dump -i <iface>\n"
            "  cantool send -i <iface> <ID十六进制> [b0 b1 ... b7]\n"
            "  cantool loopback-test -i <iface>\n"
            "  cantool stats -i <iface>\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];
    const char *iface = "can0";

    /*
     * 参数解析：把 -i <iface> 选项挑出来，剩下的按位置用。
     * ⚠️ 早先版本写死了 argv[3] 当 ID —— 带 `-i can0` 时 argv[3] 其实是 "can0"，
     *    于是 `cantool send -i can0 100 42 …` 会报"字节参数不合法: 100"
     *    （真实踩到：文档里给用户的示例命令跑不通）。
     */
    const char *pos[16];
    int npos = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iface = argv[++i];
            continue;
        }
        if (npos < (int)(sizeof(pos) / sizeof(pos[0])))
            pos[npos++] = argv[i];
    }

    if (strcmp(cmd, "stats") == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ip -d link show %s; ip -s -d link show %s; cat /proc/net/can/stats",
                 iface, iface);
        return system(buf);
    }

    const int fd = open_can(iface);
    if (fd < 0)
        return 1;

    if (strcmp(cmd, "dump") == 0) {
        signal(SIGINT, on_sigint);
        printf("# 监听 %s（Ctrl-C 退出） 格式 <ID>#<DATA>\n", iface);
        fflush(stdout);
        while (!g_stop) {
            struct can_frame f;
            const ssize_t n = read(fd, &f, sizeof(f));
            if (n < (ssize_t)sizeof(f)) {
                if (errno == EINTR)
                    continue;
                perror("read");
                break;
            }
            if (f.can_id & CAN_ERR_FLAG) {
                printf("# 错误帧 id=%08X\n", f.can_id);
                continue;
            }
            print_frame(&f);
        }
        close(fd);
        return 0;
    }

    if (strcmp(cmd, "send") == 0) {
        if (npos < 1) {
            usage();
            close(fd);
            return 2;
        }
        struct can_frame f;
        memset(&f, 0, sizeof(f));
        f.can_id = (canid_t)strtoul(pos[0], NULL, 16) & CAN_SFF_MASK;
        int dlc = 0;
        for (int i = 1; i < npos && dlc < 8; ++i) {
            unsigned char b;
            if (hexbyte(pos[i], &b) != 0) {
                fprintf(stderr, "字节参数不合法: %s\n", pos[i]);
                close(fd);
                return 2;
            }
            f.data[dlc++] = b;
        }
        f.can_dlc = (__u8)dlc;
        if (write(fd, &f, sizeof(f)) != (ssize_t)sizeof(f)) {
            perror("write");
            close(fd);
            return 1;
        }
        printf("# 已发送 "); print_frame(&f);
        close(fd);
        return 0;
    }

    if (strcmp(cmd, "loopback-test") == 0) {
        /* 注意：本板历史上回环测试曾伴随挂机（后来证明是"模块 ABI 错配"，
         * 与回环无关）。现在 CAN 已内建，如仍需自测请谨慎并使用记录。 */
        signal(SIGINT, on_sigint);
        struct can_frame f;
        memset(&f, 0, sizeof(f));
        f.can_id = 0x123;
        f.can_dlc = 8;
        for (int i = 0; i < 8; ++i)
            f.data[i] = (__u8)i;
        if (write(fd, &f, sizeof(f)) != (ssize_t)sizeof(f)) {
            perror("write");
            close(fd);
            return 1;
        }
        printf("# 已发送回环帧 123#0001020304050607，等待回读（3 秒）...\n");
        struct timeval tv = { 3, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        struct can_frame r;
        const ssize_t n = read(fd, &r, sizeof(r));
        if (n == (ssize_t)sizeof(r) && (r.can_id & CAN_SFF_MASK) == 0x123) {
            printf("OK:回环成功 "); print_frame(&r);
            close(fd);
            return 0;
        }
        printf("FAIL:3 秒内没收到自己的帧（回环模式没开？用 ip link set %s type can loopback on）\n", iface);
        close(fd);
        return 1;
    }

    usage();
    close(fd);
    return 2;
}
