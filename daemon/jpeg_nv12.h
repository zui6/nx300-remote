#ifndef JPEG_NV12_H_INCLUDED
#define JPEG_NV12_H_INCLUDED

/*
 * 把 NV12 (YUV420 semi-planar) 直接编码成 JPEG。
 *
 * NV12 的 Y/Cb/Cr 布局和 JPEG 内部的 YCbCr 4:2:0 完全一致，
 * 所以这里不需要任何色彩空间转换，只是把平面拆开喂给 libjpeg。
 *
 * nv12    : width*height*3/2 字节，Y 平面在前，UV 交错在后
 * quality : 1..100
 * scale   : 1 = 原始尺寸, 2 = 半尺寸（宽高各除2）
 * out/len : 成功时指向 malloc 出来的 JPEG 数据，调用方负责 free()
 *
 * 返回 0 成功，非 0 失败。
 */
int jpeg_nv12_encode(const unsigned char *nv12, int width, int height,
                     int quality, int scale, int expand_range,
                     unsigned char **out, unsigned long *out_len);

#endif
