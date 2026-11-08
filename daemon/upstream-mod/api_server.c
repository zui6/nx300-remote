#include "api_server.h"
#include "command.h"
#include "input.h"
#include "lcd.h"
#include "led.h"
#include "liveview.h"
#include "screen_jpeg.h"
#include "params.h"
#include "mongoose.h"
#include "network.h"
#include "nx_model.h"
#include "osd.h"
#include "shutter.h"
#include "status.h"
#include "util.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const char *s_http_port = "80";
static struct mg_serve_http_opts s_http_server_opts;

void send_200(struct mg_connection *nc)
{
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Headers: X-Requested-With\r\n"
                        "Content-Length: 0\r\n\r\n");
}

void send_404(struct mg_connection *nc)
{
    mg_printf(nc, "%s", "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: 3\r\n\r\n"
                        "404");
}

void send_500(struct mg_connection *nc)
{
    mg_printf(nc, "%s", "HTTP/1.1 500 Internal Server Error\r\n"
                        "Content-Length: 3\r\n\r\n"
                        "500");
}

static void handle_camera_api(struct mg_connection *nc,
                              struct http_message *hm,
                              struct mg_str *func)
{
    if (mg_vcmp(func, "info") == 0) {
        mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: X-Requested-With\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n\r\n");
        mg_printf_http_chunk(nc, "{\"model\":\"%s\","
                                  "\"fw_ver\":\"%s\","
                                  "\"mac_address\":\"%s\"}",
                             get_nx_model_name(),
                             get_nx_model_version(),
                             get_mac_address());
        mg_send_http_chunk(nc, "", 0);
    } else if (mg_vcmp(func, "status") == 0) {
        char json_cameras[1024];
        int json_size = network_get_discovered_cameras(
                            json_cameras, sizeof(json_cameras));
        if (json_size == -1) {
            snprintf(json_cameras, sizeof(json_cameras), "[]");
        }
        mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: X-Requested-With\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n\r\n");
        mg_printf_http_chunk(nc,
                             "{\"battery_charging\":%s,"
                             "\"battery_percent\":%d,"
                             "\"battery_level\":%d,"
                             "\"drive\":\"%s\","
                             "\"mode\":\"%s\","
                             "\"hevc\":\"%s\","
                             "\"cameras\":%s}",
                             is_battery_charging() ? "true" : "false",
                             get_battery_percent(),
                             get_battery_level(),
                             get_dial_drive_string(),
                             get_dial_mode_string(),
                             get_hevc_state_string(),
                             json_cameras);
        mg_send_http_chunk(nc, "", 0);
    } else {
        send_404(nc);
    }
}

static void handle_lcd_api(struct mg_connection *nc,
                           struct http_message *hm,
                           struct mg_str *func)
{
    if (mg_vcmp(func, "on") == 0) {
        lcd_set_state(LCD_ON);
        send_200(nc);
    } else if (mg_vcmp(func, "off") == 0) {
        lcd_set_state(LCD_OFF);
        send_200(nc);
    } else if (mg_vcmp(func, "video") == 0) {
        lcd_set_state(LCD_VIDEO);
        send_200(nc);
    } else {
        send_404(nc);
    }
}

static void handle_led_api(struct mg_connection *nc,
                           struct http_message *hm,
                           struct mg_str *func)
{
    if (mg_vcmp(func, "on") == 0) {
        led_set(true);
        send_200(nc);
    } else if (mg_vcmp(func, "off") == 0) {
        led_set(false);
        send_200(nc);
    } else {
        send_404(nc);
    }
}

static void handle_shutter_api(struct mg_connection *nc,
                               struct http_message *hm,
                               struct mg_str *func)
{
    if (mg_vcmp(func, "normal") == 0) {
        shutter_set_silent(false);
        send_200(nc);
    } else if (mg_vcmp(func, "silent") == 0) {
        shutter_set_silent(true);
        send_200(nc);
    } else {
        send_404(nc);
    }
}

static int query_int(struct http_message *hm, const char *name, int defval)
{
    const char *p = hm->query_string.p;
    int len = (int) hm->query_string.len;
    int nlen = (int) strlen(name);
    int i;

    for (i = 0; i + nlen < len; i++) {
        if ((i == 0 || p[i - 1] == '&') &&
            memcmp(p + i, name, nlen) == 0 && p[i + nlen] == '=') {
            return atoi(p + i + nlen + 1);
        }
    }
    return defval;
}
static void handle_liveview_api(struct mg_connection *nc,
                                struct http_message *hm,
                                struct mg_str *func)
{
    if (mg_vcmp(func, "get") == 0) {
        liveview_http_send(nc, hm, false);
    } else if (mg_vcmp(func, "get_low") == 0) {
        liveview_http_send(nc, hm, true);
    } else if (mg_vcmp(func, "jpeg") == 0) {
        int q = query_int(hm, "q", 70);
        int scale = query_int(hm, "scale", 1);
        if (q < 10) q = 10;
        if (q > 95) q = 95;
        if (scale < 1) scale = 1;
        if (scale > 4) scale = 4;
        liveview_http_send_jpeg(nc, q, scale, query_int(hm, "rng", 0));
    } else {
        /* 未知接口必须回包，否则客户端会一直等（mongoose 单线程，会拖死其它请求） */
        mg_printf(nc, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
}
static void handle_osd_api(struct mg_connection *nc,
                           struct http_message *hm,
                           struct mg_str *func)
{
    if (mg_ncasecmp(func->p, "get", 3) == 0) {
        osd_http_send(nc, hm);
    }
}

static void handle_input_api(struct mg_connection *nc,
                             struct http_message *hm,
                             struct mg_str *func)
{
    static const struct mg_str inject_prefix = MG_MK_STR("inject?");
    struct mg_str command_start;
    char command[128];
    char *p;

    if (mg_ncasecmp(func->p, "inject?", 7) == 0) {
        command_start.p = func->p + inject_prefix.len;
        command_start.len = func->len - inject_prefix.len;
        p = strstr(command_start.p, " ");
        strncpy(command, command_start.p, p - command_start.p);
        command[p - command_start.p] = '\0';
        p = strstr(command, "=");
        if (p != NULL) {
            *p = ' ';
        }
        p = strstr(command, "-");
        if (p != NULL) {
            *p = ' ';
        }
        print_log("command = %s", command);
        input_inject(command);

        send_200(nc);
    } else if (mg_vcmp(func, "inject_keep_alive") == 0) {
        input_inject_keep_alive();
        send_200(nc);
    }
}

static int has_prefix(const struct mg_str *uri, const struct mg_str *prefix) {
    return uri->len > prefix->len && memcmp(uri->p, prefix->p, prefix->len) == 0;
}

static void ev_handler(struct mg_connection *nc, int ev, void *p)
{
    static const struct mg_str api_prefix = MG_MK_STR("/api/v1/");
    static const struct mg_str camera_prefix = MG_MK_STR("camera/");
    static const struct mg_str liveview_prefix = MG_MK_STR("liveview/");
    static const struct mg_str param_prefix = MG_MK_STR("/api/v1/param");
static const struct mg_str params_prefix = MG_MK_STR("/api/v1/params");
    static const struct mg_str osd_prefix = MG_MK_STR("osd/");
    static const struct mg_str input_prefix = MG_MK_STR("input/");
    static const struct mg_str lcd_prefix = MG_MK_STR("lcd/");
    static const struct mg_str led_prefix = MG_MK_STR("led/");
    static const struct mg_str shutter_prefix = MG_MK_STR("shutter/");
    struct http_message *hm = (struct http_message *) p;
    struct mg_str uri;
    struct mg_str func;

    switch (ev) {
        case MG_EV_ACCEPT: {
            /* 默认 sndbuf 很小，576KB 的帧会被拆成上百次写；
               mongoose 每次写完都要 memmove 剩余数据，O(n^2)。
               放大到 512KB 后通常 1-2 次就能写完。 */
            int sz = 512 * 1024;
            int on = 1;
            setsockopt(nc->sock, SOL_SOCKET, SO_SNDBUF,
                       (const char *) &sz, sizeof(sz));
            setsockopt(nc->sock, IPPROTO_TCP, TCP_NODELAY,
                       (const char *) &on, sizeof(on));
            break;
        }
        case MG_EV_CLOSE:
            print_log("CLOSE");
            break;
        case MG_EV_HTTP_REQUEST:
            if (hm->uri.len == params_prefix.len &&
                memcmp(hm->uri.p, params_prefix.p, params_prefix.len) == 0) {
                struct cam_params cp;
                char js[256];
                if (params_read(&cp) == 0) {
                    snprintf(js, sizeof(js),
                             "{\"iso\":%d,\"iso_max\":%d,\"shutter_num\":%u,"
                             "\"shutter_den\":%u,\"aperture10\":%u,\"tv16\":%u,\"av16\":%u}",
                             cp.iso, cp.iso_max, cp.shutter_num, cp.shutter_den,
                             cp.aperture10, cp.tv16, cp.av16);
                    mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Content-Length: %d\r\n\r\n%s",
                              (int) strlen(js), js);
                } else {
                    mg_printf(nc, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
                }
                break;
            }
            if (hm->uri.len == param_prefix.len &&
                memcmp(hm->uri.p, param_prefix.p, param_prefix.len) == 0) {
                int pq = query_int(hm, "q", 80);
                int py0 = query_int(hm, "y0", 425);
                int py1 = query_int(hm, "y1", 480);
                unsigned char *pbuf = NULL;
                unsigned long plen = 0;
                long long pt0 = get_current_time();
                if (screen_capture_band(pq, py0, py1, &pbuf, &plen) == 0 && pbuf != NULL) {
                    mg_printf(nc, "HTTP/1.1 200 OK\r\nX-NX-Rebuilt: 1\r\n"
                                  "Content-Length: %d\r\n"
                                  "Content-Type: image/jpeg\r\n"
                                  "Access-Control-Allow-Origin: *\r\n\r\n", (int) plen);
                    mg_send(nc, pbuf, (int) plen);
                    free(pbuf);
                    print_log("param band %d-%d -> %lu bytes in %lld ms",
                              py0, py1, plen, get_current_time() - pt0);
                } else {
                    mg_printf(nc, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
                }
                break;
            }            print_log("REQ %.*s %.*s", (int) hm->method.len, hm->method.p,
                      (int) hm->uri.len, hm->uri.p);
            if (has_prefix(&hm->uri, &api_prefix)) {
                uri.p = hm->uri.p + api_prefix.len;
                uri.len = hm->uri.len - api_prefix.len;
                if (has_prefix(&uri, &camera_prefix)) {
                    func.p = uri.p + camera_prefix.len;
                    func.len = uri.len - camera_prefix.len;
                    handle_camera_api(nc, hm, &func);
                } else if (has_prefix(&uri, &liveview_prefix)) {
                    func.p = uri.p + liveview_prefix.len;
                    func.len = uri.len - liveview_prefix.len;
                    handle_liveview_api(nc, hm, &func);
                } else if (has_prefix(&uri, &osd_prefix)) {
                    func.p = uri.p + osd_prefix.len;
                    func.len = uri.len - osd_prefix.len;
                    handle_osd_api(nc, hm, &func);
                } else if (has_prefix(&uri, &input_prefix)) {
                    func.p = uri.p + input_prefix.len;
                    func.len = uri.len - input_prefix.len;
                    handle_input_api(nc, hm, &func);
                } else if (has_prefix(&uri, &lcd_prefix)) {
                    func.p = uri.p + lcd_prefix.len;
                    func.len = uri.len - lcd_prefix.len;
                    handle_lcd_api(nc, hm, &func);
                } else if (has_prefix(&uri, &led_prefix)) {
                    func.p = uri.p + led_prefix.len;
                    func.len = uri.len - led_prefix.len;
                    handle_led_api(nc, hm, &func);
                } else if (has_prefix(&uri, &shutter_prefix)) {
                    func.p = uri.p + shutter_prefix.len;
                    func.len = uri.len - shutter_prefix.len;
                    handle_shutter_api(nc, hm, &func);
                }

            } else {
                mg_serve_http(nc, hm, s_http_server_opts);
            }
            break;
        default:
            break;
    }
}

bool api_server_run(void)
{
    char web_root[256];
    struct mg_mgr mgr;
    struct mg_connection *nc;

    {
        const char *env_port = getenv("NX_HTTP_PORT");
        if (env_port != NULL && env_port[0] != '\0') {
            s_http_port = env_port;
        }
    }

    snprintf(web_root, sizeof(web_root), "%s/web_root", get_app_path());

    mg_mgr_init(&mgr, NULL);

    s_http_server_opts.document_root = web_root;
    s_http_server_opts.enable_directory_listing = "yes";

    nc = mg_bind(&mgr, s_http_port, ev_handler);
    if (nc == NULL) {
        print_error("mg_bind() failed!");
        mg_mgr_free(&mgr);
        return false;
    }
    mg_set_protocol_http_websocket(nc);

    print_log("Starting threaded server on port %s", s_http_port);

    for (;;) {
        if (network_get_wifi_ip_address() == NULL) {
            print_log("wifi down! stopping api server...");
            break;
        }
        mg_mgr_poll(&mgr, 5);
    }

    mg_mgr_free(&mgr);

    return true;
}
