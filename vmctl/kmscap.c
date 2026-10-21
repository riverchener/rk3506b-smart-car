/*
 * kmscap.c —— 直接读 DRM 当前扫描输出的那张 framebuffer（= 真屏）。
 *
 * 为什么需要它（前面几条路都不通，原始报错见 docs）：
 *   · /dev/fb0 读出来全黑（rockchipdrmfb 是影子缓冲，weston 走 KMS 直扫不更新它）；
 *   · weston-screenshooter 报 "display does not support weston_capture_v1" 直接退出；
 *   · 自己写的 weston_capture_v1 客户端能拿到 size 事件，但 capture 的 complete 永不来
 *     （pixman 渲染器 + 无 KMS 平面的组合下抓屏路径不通）。
 *
 * 做法：DRM_IOCTL_MODE_GETCRTC 拿到当前 crtc 的 buffer_id，
 *       再 DRM_IOCTL_MODE_MAP_DUMB + mmap 把那块缓冲读出来写 PPM。
 *       这是内核正在扫描出去的内容，肉眼看到的就是它。
 *
 * 输出：P6 PPM（尺寸由 framebuffer 决定，通常是 720x1280，因为 panel 是竖屏 +
 *       weston 做了 rotate-90；本地再旋转成 1280x720 看逻辑画面）。
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define DRM_IOCTL_MODE_GETFB2 0xc02064b6

struct drm_mode_fb_cmd2_local {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};

static int drmfd;

static int get_crtc_fb(uint32_t *fb_id_out, uint32_t *w_out, uint32_t *h_out)
{
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    if (ioctl(drmfd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("GETRESOURCES(count)");
        return -1;
    }
    uint32_t ncrtc = res.count_crtcs;
    printf("resources: crtcs=%u connectors=%u encoders=%u fbs=%u\n",
           res.count_crtcs, res.count_connectors, res.count_encoders, res.count_fbs);
    if (ncrtc == 0 || ncrtc > 64) {
        fprintf(stderr, "crtc 数量异常: %u\n", ncrtc);
        return -1;
    }

    uint32_t *crtcs = calloc(ncrtc, 4);
    uint32_t *conns = calloc(res.count_connectors ? res.count_connectors : 1, 4);
    uint32_t *encs = calloc(res.count_encoders ? res.count_encoders : 1, 4);
    uint32_t *fbs = calloc(res.count_fbs ? res.count_fbs : 1, 4);
    struct drm_mode_card_res r2;
    memset(&r2, 0, sizeof(r2));
    r2.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    r2.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    r2.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
    r2.fb_id_ptr = (uint64_t)(uintptr_t)fbs;
    r2.count_crtcs = res.count_crtcs;
    r2.count_connectors = res.count_connectors;
    r2.count_encoders = res.count_encoders;
    r2.count_fbs = res.count_fbs;
    if (ioctl(drmfd, DRM_IOCTL_MODE_GETRESOURCES, &r2) < 0) {
        perror("GETRESOURCES(data)");
        return -1;
    }

    int rc = -1;
    for (uint32_t i = 0; i < r2.count_crtcs; i++) {
        struct drm_mode_crtc c;
        memset(&c, 0, sizeof(c));
        c.crtc_id = crtcs[i];
        if (ioctl(drmfd, DRM_IOCTL_MODE_GETCRTC, &c) < 0) {
            perror("GETCRTC");
            continue;
        }
        printf("crtc %u: fb_id=%u %ux%u mode_valid=%u\n",
               crtcs[i], c.fb_id, c.mode.hdisplay, c.mode.vdisplay, c.mode_valid);
        if (c.fb_id && c.mode_valid) {
            *fb_id_out = c.fb_id;
            *w_out = c.mode.hdisplay;
            *h_out = c.mode.vdisplay;
            rc = 0;
            break;
        }
    }
    free(crtcs); free(conns); free(encs); free(fbs);
    return rc;
}

static uint32_t crtcs_dummy;

int main(int argc, char **argv)
{
    /*
     * ★ 硬规则③：默认输出路径必须是 /userdata（不是 /tmp、更不是 rootfs 或 CWD）。
     *   /tmp 实测是 tmpfs（重启即失，只适合 scratch），rootfs 只有 150 MB 且常年 99% 满
     *   —— 2026-10-13 就有一对 .ppm 被写进 rootfs 的 /home/cjh/wsflash/shots/，
     *   把可用量从 1.1 MB 顶到 72 KB（见 build_out/docs/rootfs空间告急_只读排查_20261015.md）。
     *   所以"忘了带参数"时的兜底落点也钉在 /userdata/wsflash。
     */
    const char *out = (argc > 1) ? argv[1] : "/userdata/wsflash/kms.ppm";
    const char *dev = (argc > 2) ? argv[2] : "/dev/dri/card0";

    drmfd = open(dev, O_RDWR);
    if (drmfd < 0) {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return 2;
    }

    uint32_t fb_id = 0, w = 0, h = 0;
    if (get_crtc_fb(&fb_id, &w, &h) < 0) {
        fprintf(stderr, "找不到有 framebuffer 的 crtc\n");
        return 3;
    }
    printf("live crtc framebuffer: fb_id=%u %ux%u\n", fb_id, w, h);

    /* 优先用 GETFB2（能拿到 pitch/format/modifier）；老内核回退 GETFB */
    struct drm_mode_fb_cmd2_local fb2;
    memset(&fb2, 0, sizeof(fb2));
    fb2.fb_id = fb_id;
    uint32_t handle = 0, pitch = 0, pf = 0;
    int have_fb2 = 0;
    if (ioctl(drmfd, DRM_IOCTL_MODE_GETFB2, &fb2) == 0) {
        handle = fb2.handles[0];
        pitch = fb2.pitches[0];
        pf = fb2.pixel_format;
        have_fb2 = 1;
        printf("GETFB2: handle=%u pitch=%u format=0x%08x w=%u h=%u\n",
               handle, pitch, pf, fb2.width, fb2.height);
        /* GETFB2 拿到的 handle 只用于查询，map 要用 GETFB 的 */
    }

    struct drm_mode_fb_cmd fb;
    memset(&fb, 0, sizeof(fb));
    fb.fb_id = fb_id;
    if (ioctl(drmfd, DRM_IOCTL_MODE_GETFB, &fb) < 0) {
        perror("GETFB");
        return 4;
    }
    handle = fb.handle;
    pitch = fb.pitch;
    w = fb.width;
    h = fb.height;
    printf("GETFB: handle=%u pitch=%u bpp=%u %ux%u%s\n",
           handle, pitch, fb.bpp, w, h, have_fb2 ? " (GETFB2 也成功)" : "");

    struct drm_mode_map_dumb md;
    memset(&md, 0, sizeof(md));
    md.handle = handle;
    if (ioctl(drmfd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) {
        perror("MAP_DUMB");
        return 5;
    }
    size_t size = (size_t)pitch * h;
    void *p = mmap(NULL, size, PROT_READ, MAP_SHARED, drmfd, md.offset);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 6;
    }
    printf("mapped %zu bytes at offset %llu\n", size, (unsigned long long)md.offset);

    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 7; }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    unsigned char *row = malloc((size_t)w * 3);
    const unsigned char *base = p;
    for (uint32_t y = 0; y < h; y++) {
        const unsigned char *s = base + (size_t)y * pitch;
        for (uint32_t x = 0; x < w; x++) {
            /* XRGB8888 小端 => 内存里 B,G,R,X */
            row[x * 3 + 0] = s[x * 4 + 2];
            row[x * 3 + 1] = s[x * 4 + 1];
            row[x * 3 + 2] = s[x * 4 + 0];
        }
        fwrite(row, 1, (size_t)w * 3, f);
    }
    fclose(f);
    free(row);
    munmap(p, size);
    printf("saved %s (%ux%u)\n", out, w, h);
    return 0;
}
