/*
 * jpeg_nv12.c —— NV12 -> JPEG（直接喂 YCbCr 平面，无色彩转换）
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "jpeg_nv12.h"

/* 复用的平面缓冲，避免每帧 malloc */
/* scale x scale 平均降采样：比直接抽点更平滑，噪点被平均掉后 JPEG 更小 */
static void downscale_avg_y(const unsigned char *src, int stride,
                            unsigned char *dst, int ow, int scale)
{
    int x, i, j, n = scale * scale;
    for (x = 0; x < ow; x++) {
        const unsigned char *p = src + (size_t) x * scale;
        unsigned int sum = 0;
        for (j = 0; j < scale; j++) {
            const unsigned char *row = p + (size_t) j * stride;
            for (i = 0; i < scale; i++) sum += row[i];
        }
        dst[x] = (unsigned char) (sum / n);
    }
}

static void downscale_avg_uv(const unsigned char *src, int stride,
                             unsigned char *dcb, unsigned char *dcr,
                             int cw, int scale)
{
    int x, i, j, n = scale * scale;
    for (x = 0; x < cw; x++) {
        const unsigned char *p = src + (size_t) x * scale * 2;
        unsigned int sb = 0, sr = 0;
        for (j = 0; j < scale; j++) {
            const unsigned char *row = p + (size_t) j * stride;
            for (i = 0; i < scale; i++) {
                sb += row[i * 2];
                sr += row[i * 2 + 1];
            }
        }
        dcb[x] = (unsigned char) (sb / n);
        dcr[x] = (unsigned char) (sr / n);
    }
}
static unsigned char s_lut_y[256];
static unsigned char s_lut_c[256];
static int s_lut_ready = 0;

static void init_lut(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        int y = (i - 16) * 255 / 219;
        int c = (i - 16) * 255 / 224;
        s_lut_y[i] = (unsigned char) (y < 0 ? 0 : (y > 255 ? 255 : y));
        s_lut_c[i] = (unsigned char) (c < 0 ? 0 : (c > 255 ? 255 : c));
    }
    s_lut_ready = 1;
}

static unsigned char *s_yp = NULL;
static unsigned char *s_cbp = NULL;
static unsigned char *s_crp = NULL;
static size_t s_cap = 0;

struct nx_jpeg_err {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void nx_error_exit(j_common_ptr cinfo)
{
    struct nx_jpeg_err *e = (struct nx_jpeg_err *) cinfo->err;
    char buf[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buf);
    fprintf(stderr, "[libjpeg fatal] %s\n", buf);
    longjmp(e->jb, 1);
}

static void nx_output_message(j_common_ptr cinfo)
{
    char buf[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buf);
    fprintf(stderr, "[libjpeg] %s\n", buf);
}

int jpeg_nv12_encode(const unsigned char *nv12, int width, int height,
                     int quality, int scale, int expand_range,
                     unsigned char **out, unsigned long *out_len)
{
    struct jpeg_compress_struct cinfo;
    struct nx_jpeg_err jerr;
    JSAMPROW yrows[16], cbrows[8], crrows[8];
    JSAMPARRAY planes[3];
    int ow, oh, cw, ch, x, y, row;
    size_t need;
    const unsigned char *uv;
    if (nv12 == NULL || out == NULL || out_len == NULL) return -1;
    if (!s_lut_ready) init_lut();
    if (scale < 1) scale = 1;

    ow = (width / scale) & ~1;
    oh = (height / scale) & ~15;
    if (ow < 16 || oh < 16) return -1;

    cw = ow / 2;
    ch = oh / 2;

    need = (size_t) ow * oh + 2u * (size_t) cw * ch;
    fprintf(stderr, "[jpeg] src %dx%d -> %dx%d, need %u bytes\n", width, height, ow, oh, (unsigned) need);
    if (need > s_cap) {
        free(s_yp);
        s_yp = (unsigned char *) malloc(need);
        if (s_yp == NULL) { s_cap = 0; return -1; }
        s_cbp = s_yp + (size_t) ow * oh;
        s_crp = s_cbp + (size_t) cw * ch;
        s_cap = need;
    }

    /* Y 平面：按 scale 抽样 */
    for (y = 0; y < oh; y++) {
        const unsigned char *src = nv12 + (size_t) (y * scale) * width;
        unsigned char *dst = s_yp + (size_t) y * ow;
        if (expand_range) {
            if (scale == 1) {
                for (x = 0; x < ow; x++) dst[x] = s_lut_y[src[x]];
            } else {
                for (x = 0; x < ow; x++) dst[x] = s_lut_y[src[x * scale]];
            }
        } else if (scale == 1) {
            memcpy(dst, src, (size_t) ow);
        } else {
            downscale_avg_y(src, width, dst, ow, scale);
        }
    }

    /* 色度平面：从交错的 UV 里拆出 Cb / Cr */
    uv = nv12 + (size_t) width * height;
    for (y = 0; y < ch; y++) {
        const unsigned char *src = uv + (size_t) (y * scale) * width;
        unsigned char *dcb = s_cbp + (size_t) y * cw;
        unsigned char *dcr = s_crp + (size_t) y * cw;
        if (scale == 1) {
            for (x = 0; x < cw; x++) {
                size_t off = (size_t) x * 2;
                if (expand_range) {
                    dcb[x] = s_lut_c[src[off]];
                    dcr[x] = s_lut_c[src[off + 1]];
                } else {
                    dcb[x] = src[off];
                    dcr[x] = src[off + 1];
                }
            }
        } else if (expand_range) {
            for (x = 0; x < cw; x++) {
                size_t off = (size_t) (x * scale) * 2;
                dcb[x] = s_lut_c[src[off]];
                dcr[x] = s_lut_c[src[off + 1]];
            }
        } else {
            downscale_avg_uv(src, width, dcb, dcr, cw, scale);
        }
    }

    *out = NULL;
    *out_len = 0;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = nx_error_exit;
    jerr.pub.output_message = nx_output_message;

    if (setjmp(jerr.jb)) {
        jpeg_destroy_compress(&cinfo);
        return -1;
    }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, out, out_len);

    cinfo.image_width = (JDIMENSION) ow;
    cinfo.image_height = (JDIMENSION) oh;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    cinfo.raw_data_in = TRUE;
    cinfo.dct_method = JDCT_IFAST;

    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;

    jpeg_start_compress(&cinfo, TRUE);

    planes[0] = yrows;
    planes[1] = cbrows;
    planes[2] = crrows;

    for (row = 0; row + 16 <= oh; row += 16) {
        int i;
        for (i = 0; i < 16; i++) {
            yrows[i] = s_yp + (size_t) (row + i) * ow;
        }
        for (i = 0; i < 8; i++) {
            cbrows[i] = s_cbp + (size_t) (row / 2 + i) * cw;
            crrows[i] = s_crp + (size_t) (row / 2 + i) * cw;
        }
        jpeg_write_raw_data(&cinfo, planes, 16);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return (*out != NULL && *out_len > 0) ? 0 : -1;
}
