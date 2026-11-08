#ifndef SCREEN_JPEG_H_INCLUDED
#define SCREEN_JPEG_H_INCLUDED

int screen_capture_jpeg(int quality, int rot, unsigned char **out, unsigned long *out_len);
int screen_capture_rgb(int rot, unsigned char **out, int *out_w, int *out_h);
int screen_capture_band(int quality, int y0, int y1, unsigned char **out, unsigned long *out_len);

#endif