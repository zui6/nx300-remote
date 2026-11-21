/*
 * rawtest.c -- 主机端验证 liveview_http_send_raw 的降采样逻辑（含边界检查）
 *
 * 复制 daemon 里那段降采样代码，用合成 NV12 跑所有 scale，配 ASan 检查越界。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *s_nv12_mem;
static int g_fw = 800, g_fh = 480;

/* 与 liveview.c 中完全一致的逻辑（去掉发送部分，写入调用者缓冲） */
static size_t raw_downscale(unsigned char *dst, size_t cap, int scale)
{
    int fw = g_fw, fh = g_fh;
    int ow, oh, x, y;
    size_t need;

    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    ow = (fw / scale) & ~1;
    oh = (fh / scale) & ~1;
    if (ow < 16 || oh < 16) return 0;

    need = (size_t) ow * oh + 2u * (size_t) (ow / 2) * (oh / 2);
    if (need > cap) { fprintf(stderr, "buffer too small: need %zu cap %zu\n", need, cap); return 0; }

    if (scale == 1) {
        size_t len = (size_t) ow * oh * 3 / 2;
        if (len > cap) { fprintf(stderr, "scale1 too big\n"); return 0; }
        memcpy(dst, s_nv12_mem, len);
        return len;
    }

    for (y = 0; y < oh; y++) {
        const unsigned char *src = s_nv12_mem + (size_t) (y * scale) * fw;
        unsigned char *d = dst + (size_t) y * ow;
        for (x = 0; x < ow; x++) d[x] = src[x * scale];
    }
    {
        const unsigned char *uv = s_nv12_mem + (size_t) fw * fh;
        unsigned char *d = dst + (size_t) ow * oh;
        for (y = 0; y < oh / 2; y++) {
            const unsigned char *src = uv + (size_t) (y * scale) * fw;
            for (x = 0; x < ow / 2; x++) {
                d[x * 2] = src[(x * scale) * 2];
                d[x * 2 + 1] = src[(x * scale) * 2 + 1];
            }
            d += ow;
        }
    }
    return need;
}

/* 边界哨兵检查：分配 need + 64，末尾填 0xAA，跑完后确认没被改写 */
#define GUARD 64
static int check_scale(int scale)
{
    int fw = g_fw, fh = g_fh;
    int ow = (fw / scale) & ~1, oh = (fh / scale) & ~1;
    size_t need = (size_t) ow * oh + 2u * (size_t) (ow / 2) * (oh / 2);
    size_t cap = need + GUARD;
    unsigned char *buf = (unsigned char *) malloc(cap);
    if (!buf) return -1;
    memset(buf, 0xAA, cap);
    size_t got = raw_downscale(buf, cap, scale);
    int bad = 0;
    if (got == 0) bad = 1;
    for (size_t i = need; i < cap; i++) if (buf[i] != 0xAA) { bad = 2; break; }
    /* 抽查：Y 和 UV 都有数据 */
    if (!bad) {
        for (size_t i = 0; i < need; i++) if (buf[i] != 0xAA) break;
    }
    printf("  scale=%d: %dx%d need=%zu -> %s\n", scale, ow, oh, need,
           bad == 0 ? "OK（无越界）" : (bad == 1 ? "返回0" : "越界写！"));
    free(buf);
    return bad;
}

int main(void)
{
    size_t n = (size_t) g_fw * g_fh * 3 / 2;
    s_nv12_mem = (unsigned char *) malloc(n);
    if (!s_nv12_mem) { printf("alloc fail\n"); return 1; }
    for (size_t i = 0; i < n; i++) s_nv12_mem[i] = (unsigned char) (i * 7);

    printf("合成 %dx%d NV12（%zu 字节），测试各档 scale：\n", g_fw, g_fh, n);
    int worst = 0;
    for (int s = 1; s <= 4; s++) { int r = check_scale(s); if (r > worst) worst = r; }
    printf("结论: %s\n", worst == 0 ? "全部通过，无越界" : "存在问题");
    free(s_nv12_mem);
    return worst;
}
