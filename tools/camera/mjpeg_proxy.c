/*
 * mjpeg_proxy.c -- 把 daemon 的「每帧一次 HTTP JPEG」转成「一条长连接推流」
 *
 *   浏览器 <--multipart/x-mixed-replace-- 本程序 --GET /api/v1/liveview/jpeg--> daemon
 *
 * 设计要点
 *   1) 浏览器侧是【一条长连接】：这才是瓶颈所在（WiFi 每次往返约 30ms）。
 *   2) daemon 侧是【每帧一条短连接】：loopback 建连只要 ~0.2ms，
 *      换来"绝不复用坏连接"的简单性（早先 keep-alive 版本在多客户端时互相饿死）。
 *   3) 画质档从请求 URL 读，不再开多个进程：
 *        http://相机:8081/?scale=3&q=70   266x160
 *        http://相机:8081/?scale=2&q=70   400x240
 *   4) 内存上限：一帧 JPEG（<=128KB）+ 64KB 发送缓冲，约 200KB。
 *      相机空闲内存只有 5–9MB，这个量级是安全的。
 *
 * 用法: mjpeg_proxy <端口> [默认scale] [默认q] [fps]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define JPEG_MAX   (128 * 1024)
#define REQ_MAX    2048

static int g_scale = 3;
static int g_quality = 70;
static int g_fps = 25;

static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static int listen_on(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    struct sockaddr_in a;

    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *) &a, sizeof(a)) < 0) return -1;
    if (listen(fd, 4) < 0) return -1;
    return fd;
}

/* 大小写不敏感的子串查找（musl 不保证有 strcasestr） */
static char *find_ci(char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    char *p;
    for (p = hay; *p; p++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char) (a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char) (b - 'A' + 'a');
            if (!a || a != b) break;
        }
        if (i == nlen) return p;
    }
    return NULL;
}

static int connect_daemon(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    struct timeval tv = { 2, 0 };

    if (fd < 0) return -1;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(80);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *) &a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* 从 daemon 取一帧 JPEG；返回字节数，失败返回负数错误码 */
static int fetch_jpeg(int scale, int quality, unsigned char *buf, int cap)
{
    int fd = connect_daemon();
    char req[192];
    char hdr[1024];
    int n, hlen = 0, clen = -1, got = 0, found = 0;

    if (fd < 0) return -1;

    n = snprintf(req, sizeof(req),
                 "GET /api/v1/liveview/jpeg?q=%d&scale=%d HTTP/1.1\r\n"
                 "Host: 127.0.0.1\r\nConnection: close\r\n\r\n",
                 quality, scale);
    if (send(fd, req, n, 0) != n) { close(fd); return -2; }

    while (hlen < (int) sizeof(hdr) - 1) {
        int r = recv(fd, hdr + hlen, sizeof(hdr) - 1 - hlen, 0);
        if (r <= 0) { close(fd); return -3; }
        hlen += r;
        hdr[hlen] = '\0';
        if (strstr(hdr, "\r\n\r\n")) { found = 1; break; }
    }

    {
        char *body, *p;
        int off, have;

        if (!found) { close(fd); return -4; }
        if (!strstr(hdr, "200")) { close(fd); return -5; }

        p = find_ci(hdr, "Content-Length:");
        if (p) clen = atoi(p + 15);
        if (clen <= 0 || clen > cap) { close(fd); return -6; }

        body = strstr(hdr, "\r\n\r\n");
        if (body == NULL) { close(fd); return -7; }
        off = (int) (body - hdr) + 4;
        have = hlen - off;
        if (have > 0) {
            if (have > clen) have = clen;
            if (have > cap) { close(fd); return -7; }
            memcpy(buf, hdr + off, have);
            got = have;
        }
    }

    while (got < clen) {
        int r = recv(fd, buf + got, clen - got, 0);
        if (r <= 0) { close(fd); return -8; }
        got += r;
    }

    close(fd);
    return got;
}

/* 读请求行，取出 scale/q。返回 1 表示确实收到请求（不是空闲预连接）。 */
static int parse_request(int fd, int *scale, int *quality)
{
    char req[REQ_MAX];
    int n;
    char *p;
    struct timeval tv = { 0, 120000 };   /* 120ms：真实请求够用，空闲连接会超时 */

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return 0;
    req[n] = '\0';

    p = strstr(req, "scale=");
    if (p) {
        int v = atoi(p + 6);
        if (v >= 1 && v <= 4) *scale = v;
    }
    p = strstr(req, "q=");
    if (p) {
        int v = atoi(p + 2);
        if (v >= 10 && v <= 95) *quality = v;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8081;
    int ls, client = -1;
    unsigned char *jpeg;
    double interval, next_t;

    if (argc > 2) g_scale = atoi(argv[2]);
    if (argc > 3) g_quality = atoi(argv[3]);
    if (argc > 4) g_fps = atoi(argv[4]);
    if (g_scale < 1) g_scale = 1;
    if (g_scale > 4) g_scale = 4;
    if (g_quality < 10) g_quality = 10;
    if (g_quality > 95) g_quality = 95;
    if (g_fps < 1) g_fps = 1;
    if (g_fps > 30) g_fps = 30;

    /* 客户端断开时 send() 会触发 SIGPIPE，默认直接杀死进程 */
    signal(SIGPIPE, SIG_IGN);

    jpeg = malloc(JPEG_MAX);
    if (jpeg == NULL) {
        fprintf(stderr, "mjpeg_proxy: malloc failed\n");
        return 1;
    }

    ls = listen_on(port);
    if (ls < 0) {
        fprintf(stderr, "mjpeg_proxy: listen %d failed\n", port);
        return 1;
    }
    fprintf(stderr, "mjpeg_proxy: :%d default scale=%d q=%d fps=%d\n",
            port, g_scale, g_quality, g_fps);

    interval = 1.0 / g_fps;
    next_t = now_sec();

    for (;;) {
        static const char hdr[] =
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
            "Pragma: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";

        /* 非阻塞看有没有新连接：有就踢掉旧的，永远服务最新的那个。
           浏览器刷新/切档时旧连接会残留，不踢掉的话新连接一直排队。 */
        {
            fd_set rf;
            struct timeval zero = { 0, 0 };
            int nf;
            FD_ZERO(&rf);
            FD_SET(ls, &rf);
            if (select(ls + 1, &rf, NULL, NULL, &zero) > 0) {
                nf = accept(ls, NULL, NULL);
                if (nf >= 0) {
                    int on = 1, sz = 64 * 1024;
                    if (client >= 0) {
                        close(client);
                        client = -1;
                        fprintf(stderr, "mjpeg_proxy: client replaced\n");
                    }
                    setsockopt(nf, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
                    setsockopt(nf, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
                    {
                        int s = g_scale, q = g_quality;
                        if (!parse_request(nf, &s, &q)) {
                            close(nf);      /* 空闲预连接：丢掉，别踢掉正在用的流 */
                            goto after_accept;
                        }
                        g_scale = s;
                        g_quality = q;
                    }
                    if (send(nf, hdr, sizeof(hdr) - 1, 0) == (int) (sizeof(hdr) - 1)) {
                        client = nf;
                        next_t = 0;
                        fprintf(stderr, "mjpeg_proxy: client connected (scale=%d q=%d)\n",
                                g_scale, g_quality);
                    } else {
                        close(nf);
                    }
                }
            }
        }

    after_accept:
        if (client < 0) {
            /* 没有客户端就阻塞等（不烧 CPU） */
            int fd = accept(ls, NULL, NULL);
            int on = 1, sz = 64 * 1024;
            if (fd < 0) continue;

            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));

            {
                int s = g_scale, q = g_quality;
                if (!parse_request(fd, &s, &q)) { close(fd); continue; }
                g_scale = s;
                g_quality = q;
            }

            if (send(fd, hdr, sizeof(hdr) - 1, 0) != (int) (sizeof(hdr) - 1)) {
                close(fd);
                continue;
            }
            client = fd;
            next_t = 0;
            fprintf(stderr, "mjpeg_proxy: client connected (scale=%d q=%d)\n",
                    g_scale, g_quality);
        }

        {
            double t = now_sec();
            if (t < next_t) {
                usleep((useconds_t) ((next_t - t) * 1000000));
                continue;
            }
            next_t = now_sec() + interval;
        }

        {
            int len = fetch_jpeg(g_scale, g_quality, jpeg, JPEG_MAX);
            if (len <= 0) {
                static int fails;
                if ((fails++ % 20) == 0)
                    fprintf(stderr, "mjpeg_proxy: fetch failed code=%d n=%d\n", len, fails);
                continue;
            }

            {
                char part[128];
                int n = snprintf(part, sizeof(part),
                                 "--frame\r\nContent-Type: image/jpeg\r\n"
                                 "Content-Length: %d\r\n\r\n", len);
                struct timeval tv = { 0, 500000 };   /* 0.5s：卡住的客户端尽快丢弃 */
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                if (send(client, part, n, 0) != n ||
                    send(client, jpeg, len, 0) != len ||
                    send(client, "\r\n", 2, 0) != 2) {
                    fprintf(stderr, "mjpeg_proxy: client gone\n");
                    close(client);
                    client = -1;
                }
            }
        }
    }
    return 0;
}
