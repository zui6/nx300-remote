/*
 * screen_jpeg.c —— 抓取相机 LCD 的完整界面（含菜单/OSD），编码成 JPEG
 *
 * 传感器 buffer 里只有画面、没有 UI，所以界面视图必须走 X11：
 *   chroot tools xwd -root  ->  XWD 文件  ->  解析  ->  RGB(可旋转)  ->  libjpeg
 *
 * XWD 头字段全部是大端 32 位整数，像素数据在 header_size + ncolors*12 处。
 * 相机 LCD 的 X 缓冲是竖着的 480x800，rot=1/2 时转成 800x480。
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "screen_jpeg.h"

#define XWD_CMD "chroot /mnt/mmc/remote/tools xwd -root -silent 2>/dev/null"
#define MAX_W 1920
#define MAX_H 1200

struct scr_err {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void scr_error_exit(j_common_ptr cinfo)
{
    struct scr_err *e = (struct scr_err *) cinfo->err;
    char buf[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buf);
    fprintf(stderr, "[screen-jpeg] %s\n", buf);
    longjmp(e->jb, 1);
}

static unsigned int be32(const unsigned char *p)
{
    return ((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) |
           ((unsigned int) p[2] << 8) | (unsigned int) p[3];
}

static size_t grab_xwd(unsigned char **out)
{
    FILE *fp;
    unsigned char *buf;
    size_t cap = 2 * 1024 * 1024, len = 0, n;

    fp = popen(XWD_CMD, "r");
    if (fp == NULL) return 0;

    buf = (unsigned char *) malloc(cap);
    if (buf == NULL) { pclose(fp); return 0; }

    while (!feof(fp)) {
        if (len == cap) {
            unsigned char *nb;
            cap *= 2;
            nb = (unsigned char *) realloc(buf, cap);
            if (nb == NULL) { free(buf); pclose(fp); return 0; }
            buf = nb;
        }
        n = fread(buf + len, 1, cap - len, fp);
        if (n == 0) break;
        len += n;
        if (len > 32 * 1024 * 1024) break;
    }
    pclose(fp);

    if (len < 200) { free(buf); return 0; }
    *out = buf;
    return len;
}

int screen_capture_rgb(int rot, unsigned char **out, int *out_w, int *out_h)
{
    unsigned char *xwd = NULL, *rgb = NULL, *rotated = NULL;
    size_t xwd_len;
    unsigned int header_size, width, height, bpp, bpl, ncolors, pix_off;
    unsigned char *pix;
    unsigned int ow, oh, x, y;

    if (out == NULL || out_w == NULL || out_h == NULL) return -1;
    *out = NULL;

    xwd_len = grab_xwd(&xwd);
    if (xwd_len == 0) { fprintf(stderr, "[screen] xwd failed\n"); return -1; }

    header_size = be32(xwd + 0);
    width = be32(xwd + 16);
    height = be32(xwd + 20);
    bpp = be32(xwd + 44);
    bpl = be32(xwd + 48);
    ncolors = be32(xwd + 76);

    if (width == 0 || height == 0 || width > MAX_W || height > MAX_H) {
        fprintf(stderr, "[screen] bad size %ux%u\n", width, height);
        free(xwd);
        return -1;
    }
    pix_off = header_size + ncolors * 12;
    if ((size_t) pix_off + (size_t) bpl * height > xwd_len) {
        fprintf(stderr, "[screen] truncated\n");
        free(xwd);
        return -1;
    }
    pix = xwd + pix_off;

    rgb = (unsigned char *) malloc((size_t) width * height * 3);
    if (rgb == NULL) { free(xwd); return -1; }

    for (y = 0; y < height; y++) {
        const unsigned char *srow = pix + (size_t) y * bpl;
        unsigned char *drow = rgb + (size_t) y * width * 3;
        for (x = 0; x < width; x++) {
            if (bpp == 32) {
                drow[x * 3 + 0] = srow[x * 4 + 2];
                drow[x * 3 + 1] = srow[x * 4 + 1];
                drow[x * 3 + 2] = srow[x * 4 + 0];
            } else if (bpp == 24) {
                drow[x * 3 + 0] = srow[x * 3 + 2];
                drow[x * 3 + 1] = srow[x * 3 + 1];
                drow[x * 3 + 2] = srow[x * 3 + 0];
            } else {
                drow[x * 3 + 0] = drow[x * 3 + 1] = drow[x * 3 + 2] = 0;
            }
        }
    }

    if (rot == 1 || rot == 2) {
        ow = height; oh = width;
        rotated = (unsigned char *) malloc((size_t) ow * oh * 3);
        if (rotated == NULL) { free(rgb); free(xwd); return -1; }
        for (y = 0; y < oh; y++) {
            for (x = 0; x < ow; x++) {
                unsigned int sx, sy;
                if (rot == 1) { sx = y; sy = height - 1 - x; }
                else          { sx = width - 1 - y; sy = x; }
                memcpy(rotated + ((size_t) y * ow + x) * 3,
                       rgb + ((size_t) sy * width + sx) * 3, 3);
            }
        }
        free(rgb);
    } else {
        ow = width; oh = height;
        rotated = rgb;
    }

    free(xwd);
    *out = rotated;
    *out_w = (int) ow;
    *out_h = (int) oh;
    return 0;
}

int screen_capture_jpeg(int quality, int rot, unsigned char **out, unsigned long *out_len)
{
    unsigned char *rgb = NULL;
    int w = 0, h = 0, y;
    struct jpeg_compress_struct cinfo;
    struct scr_err jerr;
    JSAMPROW rowp[1];
    int ok = -1;

    if (out == NULL || out_len == NULL) return -1;
    *out = NULL;
    *out_len = 0;

    if (screen_capture_rgb(rot, &rgb, &w, &h) != 0) return -1;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = scr_error_exit;
    if (setjmp(jerr.jb)) { jpeg_destroy_compress(&cinfo); free(rgb); return -1; }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, out, out_len);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    cinfo.dct_method = JDCT_IFAST;
    jpeg_start_compress(&cinfo, TRUE);

    for (y = 0; y < h; y++) {
        rowp[0] = rgb + (size_t) y * w * 3;
        jpeg_write_scanlines(&cinfo, rowp, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    ok = (*out != NULL && *out_len > 0) ? 0 : -1;
    free(rgb);
    return ok;
}
/* 抓取并裁剪一条横带（rot=2 后的坐标），用于参数栏。内存只留 band 本身。 */
int screen_capture_band(int quality, int y0, int y1,
                        unsigned char **out, unsigned long *out_len)
{
    unsigned char *rgb = NULL;
    unsigned char *band = NULL;
    int w = 0, h = 0, ow, oh, y, x;
    struct jpeg_compress_struct cinfo;
    struct scr_err jerr;
    JSAMPROW rowp[1];
    int ok = -1;

    if (out == NULL || out_len == NULL) return -1;
    *out = NULL;
    *out_len = 0;

    if (screen_capture_rgb(2, &rgb, &w, &h) != 0) return -1;
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    oh = y1 - y0;
    if (oh <= 0 || w <= 0) { free(rgb); return -1; }
    ow = w;

    band = (unsigned char *) malloc((size_t) ow * oh * 3);
    if (band == NULL) { free(rgb); return -1; }

    /* rot=2 的输出已经是纵向摆正的 800x480，这里直接按行拷 */
    memcpy(band, rgb + (size_t) y0 * ow * 3, (size_t) ow * oh * 3);
    free(rgb);

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = scr_error_exit;
    if (setjmp(jerr.jb)) { jpeg_destroy_compress(&cinfo); free(band); return -1; }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, out, out_len);
    cinfo.image_width = ow;
    cinfo.image_height = oh;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    cinfo.dct_method = JDCT_IFAST;
    jpeg_start_compress(&cinfo, TRUE);

    for (y = 0; y < oh; y++) {
        rowp[0] = band + (size_t) y * ow * 3;
        jpeg_write_scanlines(&cinfo, rowp, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    ok = (*out != NULL && *out_len > 0) ? 0 : -1;
    free(band);
    return ok;
}