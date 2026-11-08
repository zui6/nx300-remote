/*
 * ui_overlay.c —— 把相机 UI 图层叠加到传感器画面上
 *
 * 思路：UI 图层几乎不变，抓一次缓存住（xwd 要 ~400ms），
 * 之后每帧只做「叠 10-15ms + 编码」，就能跑到 15-25fps。
 * 用户按键后再刷新一次缓存。
 *
 * 透明判定：xwd 抓到的 UI 里，视频区域是纯黑，所以"够亮的像素"= UI。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screen_jpeg.h"
#include "ui_overlay.h"

static unsigned char *s_rgb = NULL;
static unsigned char *s_mask = NULL;
static int s_w = 0, s_h = 0;
static int s_valid = 0;

#define UI_THRESHOLD 90   /* r+g+b 超过这个值才算 UI 像素 */

int ui_overlay_valid(void)
{
    return s_valid;
}

int ui_overlay_refresh(void)
{
    unsigned char *rgb = NULL;
    unsigned char *mask;
    int w = 0, h = 0, i, n;

    if (screen_capture_rgb(2, &rgb, &w, &h) != 0) return -1;
    if (w <= 0 || h <= 0) { free(rgb); return -1; }

    n = w * h;
    mask = (unsigned char *) malloc((size_t) n);
    if (mask == NULL) { free(rgb); return -1; }

    for (i = 0; i < n; i++) {
        int sum = rgb[i * 3] + rgb[i * 3 + 1] + rgb[i * 3 + 2];
        mask[i] = (sum > UI_THRESHOLD) ? 1 : 0;
    }

    free(s_rgb);
    free(s_mask);
    s_rgb = rgb;
    s_mask = mask;
    s_w = w;
    s_h = h;
    s_valid = 1;
    return 0;
}

static int clampi(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void ui_overlay_blend(unsigned char *nv12, int w, int h)
{
    unsigned char *Y, *uv;
    int x, y;

    if (!s_valid || s_w != w || s_h != h) return;

    Y = nv12;
    uv = nv12 + (size_t) w * h;

    for (y = 0; y < h; y++) {
        int mrow = y * w;
        for (x = 0; x < w; x++) {
            int r, g, b, yy, cb, cr, ci;
            if (!s_mask[mrow + x]) continue;

            r = s_rgb[(mrow + x) * 3 + 0];
            g = s_rgb[(mrow + x) * 3 + 1];
            b = s_rgb[(mrow + x) * 3 + 2];

            yy = (77 * r + 150 * g + 29 * b) >> 8;
            cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
            cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);

            Y[mrow + x] = (unsigned char) clampi(yy);

            ci = (y >> 1) * (w >> 1) + (x >> 1);
            uv[ci * 2] = (unsigned char) clampi(cb);
            uv[ci * 2 + 1] = (unsigned char) clampi(cr);
        }
    }
}
