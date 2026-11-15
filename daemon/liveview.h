#ifndef LIVEVIEW_H_INCLUDED
#define LIVEVIEW_H_INCLUDED

#include <stdbool.h>

#include "mongoose.h"

extern void liveview_init(void);
extern void liveview_destroy(void);
extern void liveview_http_send(struct mg_connection *nc,
                               struct http_message *hm,
                               bool reduce_size);

void liveview_http_send_jpeg(struct mg_connection *nc, int quality, int scale, int expand_range);

/* 原始 NV12 通道：只做降采样，不做 JPEG 编码，编码压力转移到客户端 */
void liveview_http_send_raw(struct mg_connection *nc, int scale);

void liveview_http_send_ui_jpeg(struct mg_connection *nc, int quality, int scale,
                                int expand_range, int refresh);

#endif
