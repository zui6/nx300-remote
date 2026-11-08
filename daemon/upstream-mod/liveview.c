#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mongoose.h"
#include "nx_model.h"
#include "osd.h"
#include "jpeg_nv12.h"
#include "ui_overlay.h"
#include "status.h"
#include "util.h"

#define DEV_MEM_PATH "/dev/mem"

static int s_num_video_addrs;
static void **s_addrs;
static int *s_hashs;
static int s_fd;
static unsigned char *s_nv12_mem;

static void *mmap_lcd(const int fd, const off_t offset)
{
    off_t pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    //print_log("offset = %llu, pa_offset = %llu", (unsigned long long)offset, (unsigned long long)pa_offset);
    const size_t length = get_mmap_video_size();
    void *p = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, pa_offset);
    if (p == MAP_FAILED) {
        die("mmap() failed");
    }

    return p + (offset - pa_offset);
}

static void munmap_lcd(void *addr, const off_t offset)
{
    off_t pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    const size_t length = get_mmap_video_size();
    if (munmap(addr - (offset - pa_offset), length) == -1) {
        die("munmap() failed");
    }
}

void liveview_init(void)
{
    int i;

    s_num_video_addrs = get_num_video_addrs();

    s_addrs = (void **)malloc(sizeof(void *) * s_num_video_addrs);
    s_hashs = (int *)calloc(s_num_video_addrs, sizeof(int));

    if (s_addrs == NULL || s_hashs == NULL) {
        die("memory allocation failed.");
    }

    s_fd = open(DEV_MEM_PATH, O_RDWR);
    if (s_fd == -1) {
        die("open() error");
    }

    for (i = 0; i < s_num_video_addrs; i++) {
        s_addrs[i] = mmap_lcd(s_fd, get_video_addr(i));
        print_log("phy addr = 0x%lx --> addr = 0x%p",
                  get_video_addr(i), s_addrs[i]);
    }
}

void liveview_destroy(void)
{
    int i;

    for (i = 0; i < s_num_video_addrs; i++) {
        munmap_lcd(s_addrs[i], get_video_addr(i));
    }

    if (close(s_fd) == -1) {
        print_error("close failed");
    }

    free(s_addrs);
    free(s_hashs);
}

static void liveview_get(bool low_quality,
                         int *frame_width, int *frame_height, int *frame_size)
{
    int i, j, hash;
    HevcState hevc_state;

    *frame_width = get_frame_width();
    *frame_height = get_frame_height();

    hevc_state = get_hevc_state();
    if (hevc_state == HEVC_STATE_ON) {
        MovieSize movie_size = get_movie_size();
        //print_log("movie_size = %d", movie_size);
        if (movie_size == MOVIE_SIZE_4K) {
            *frame_height = 380;
        } else if (movie_size == MOVIE_SIZE_UHD_FHD_HD) {
            *frame_height = 404;
        } else if (movie_size == MOVIE_SIZE_VGA) {
            *frame_width = 640;
        }
    }

    if (low_quality) {
        *frame_size = *frame_width * *frame_height / 4 * 3;
    } else {
        *frame_size = *frame_width * *frame_height * 3 / 2;
    }

    for (i = 0; i < s_num_video_addrs; i++) {
        unsigned char *p = s_addrs[i];

        hash = 0;
        for (j = 0; j < *frame_width * 2; j++) {
            hash += p[j];
        }
        if (s_hashs[i] != 0 && hash != s_hashs[i]) {
            s_nv12_mem = p;
            //print_log("[%d] framesize = %d", i, *frame_size);
        }

        s_hashs[i] = hash;
    }

    /* 刚启动时一帧都没变化过，s_nv12_mem 还是 NULL，
       客户端第一次请求会拿到 0 字节。回退到第一个缓冲区。 */
    if (s_nv12_mem == NULL && s_num_video_addrs > 0) {
        s_nv12_mem = s_addrs[0];
    }
}

static void reduce_ys(unsigned char *ys, int frame_width, int frame_height)
{
    int i, x, y;

    for (i = 0, y = 0; y < frame_height; y+=2) {
        for (x = 0; x < frame_width; x+=2) {
            ys[i++] = s_nv12_mem[y * frame_width + x];
        }
    }
}

void liveview_http_send(struct mg_connection *nc,
                        struct http_message *hm,
                        bool reduce_size)
{
    int frame_width, frame_height, frame_size;
    unsigned char ys[800*480/4];
    int mbuf_before = 0;
    long long start_time = get_current_time();
    long long end_time;

    if (is_nx1() && osd_is_evf()) {
        mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: X-Requested-With\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/plain; charset=x-user-defined\r\n"
                      "\r\n", 0);
        return;
    }

    liveview_get(reduce_size, &frame_width, &frame_height, &frame_size);

    if (s_nv12_mem != NULL) {
        if (reduce_size) {
            reduce_ys(ys, frame_width, frame_height);
        }

        mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\nContent-Length: %d\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: X-Requested-With\r\n"
                      "Content-Type: text/plain; charset=x-user-defined\r\n"
                      "\r\n", frame_size);
        mbuf_before = (int) nc->send_mbuf.len;
        if (reduce_size) {
            mg_send(nc, ys, frame_width * frame_height / 4);
            mg_send(nc, s_nv12_mem + (frame_width * frame_height),
                    frame_width * frame_height / 4 * 2);
        } else {
            mg_send(nc, s_nv12_mem, frame_size);
        }
        print_log("send_mbuf %d -> %d (+%d), want %d, w=%d h=%d", mbuf_before,
                  (int) nc->send_mbuf.len,
                  (int) nc->send_mbuf.len - mbuf_before,
                  frame_size, frame_width, frame_height);
    } else {
        mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: X-Requested-With\r\n"
                       "Content-Length: %d\r\n"
                      "Content-Type: text/plain; charset=x-user-defined\r\n"
                      "\r\n", 0);
    }

    end_time = get_current_time();
    print_log("framesize = %d, time = %lld",
              frame_size, end_time - start_time);
}

void liveview_http_send_jpeg(struct mg_connection *nc, int quality, int scale, int expand_range)
{
    int frame_width, frame_height, frame_size;
    unsigned char *buf = NULL;
    unsigned long len = 0;
    long long t0, t1;

    liveview_get(false, &frame_width, &frame_height, &frame_size);

    if (s_nv12_mem == NULL) {
        mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                      "Content-Length: 0\r\n"
                      "Content-Type: image/jpeg\r\n"
                      "Access-Control-Allow-Origin: *\r\n\r\n");
        return;
    }

    t0 = get_current_time();
    if (jpeg_nv12_encode(s_nv12_mem, frame_width, frame_height,
                         quality, scale, expand_range, &buf, &len) != 0 || buf == NULL) {
        mg_printf(nc, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        print_log("jpeg encode failed");
        return;
    }
    t1 = get_current_time();

    mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                  "Content-Length: %d\r\n"
                  "Content-Type: image/jpeg\r\n"
                  "Access-Control-Allow-Origin: *\r\n\r\n", (int) len);
    mg_send(nc, buf, (int) len);
    free(buf);

    print_log("jpeg q=%d scale=%d src=%dx%d -> %lu bytes in %lld ms",
              quality, scale, frame_width, frame_height, len, t1 - t0);
}
void liveview_http_send_ui_jpeg(struct mg_connection *nc, int quality, int scale,
                                int expand_range, int refresh)
{
    static unsigned char *s_frame = NULL;
    static size_t s_frame_cap = 0;
    int frame_width, frame_height, frame_size, need;
    unsigned char *buf = NULL;
    unsigned long len = 0;
    long long t0, t1;

    if (refresh || !ui_overlay_valid()) {
        t0 = get_current_time();
        if (ui_overlay_refresh() == 0) {
            print_log("ui refreshed in %lld ms", get_current_time() - t0);
        }
    }

    liveview_get(false, &frame_width, &frame_height, &frame_size);
    if (s_nv12_mem == NULL) {
        mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                      "Content-Length: 0\r\nContent-Type: image/jpeg\r\n"
                      "Access-Control-Allow-Origin: *\r\n\r\n");
        return;
    }

    need = frame_width * frame_height * 3 / 2;
    if ((size_t) need > s_frame_cap) {
        free(s_frame);
        s_frame = (unsigned char *) malloc((size_t) need);
        if (s_frame == NULL) { s_frame_cap = 0; goto fail; }
        s_frame_cap = (size_t) need;
    }
    memcpy(s_frame, s_nv12_mem, (size_t) need);

    t0 = get_current_time();
    ui_overlay_blend(s_frame, frame_width, frame_height);
    if (jpeg_nv12_encode(s_frame, frame_width, frame_height, quality, scale,
                         expand_range, &buf, &len) != 0 || buf == NULL) {
        goto fail;
    }
    t1 = get_current_time();

    mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                  "Content-Length: %d\r\nContent-Type: image/jpeg\r\n"
                  "Access-Control-Allow-Origin: *\r\n\r\n", (int) len);
    mg_send(nc, buf, (int) len);
    free(buf);
    print_log("ui_jpeg q=%d scale=%d -> %lu bytes in %lld ms",
              quality, scale, len, t1 - t0);
    return;

fail:
    mg_printf(nc, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
}