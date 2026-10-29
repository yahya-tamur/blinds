#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "alarms.h"
#include "clock.h"
#include "events.h"
#include "logbuf.h"
#include "motor.h"
#include "web.h"

static const char *TAG = "web";

// The files in web/, embedded by main/CMakeLists.txt (EMBED_TXTFILES adds a terminating zero)
#define EMBEDDED(name) \
    extern const char name##_start[] asm("_binary_" #name "_start"); \
    extern const char name##_end[] asm("_binary_" #name "_end");
EMBEDDED(index_html)
EMBEDDED(moving_html)
EMBEDDED(style_css)
EMBEDDED(favicon_svg)

typedef struct {
    const char *uri, *type, *start, *end;
} page_t;

static const page_t pages[] = {
    { "/", "text/html", index_html_start, index_html_end },
    { "/moving", "text/html", moving_html_start, moving_html_end },
    { "/style.css", "text/css", style_css_start, style_css_end },
    { "/favicon.svg", "image/svg+xml", favicon_svg_start, favicon_svg_end },
    { "/favicon.ico", "image/svg+xml", favicon_svg_start, favicon_svg_end },   // some browsers ask regardless
};

static esp_err_t send_page(httpd_req_t *req)
{
    const page_t *p = req->user_ctx;
    httpd_resp_set_type(req, p->type);
    // Small files on a local network: always check for a newer copy rather than cache them
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, p->start, p->end - p->start - 1);
}

// Response text goes out in chunks through this buffer
typedef struct {
    httpd_req_t *req;
    char buf[512];
    size_t len;
} out_t;

static void out_flush(out_t *o)
{
    if (o->len) httpd_resp_send_chunk(o->req, o->buf, o->len);
    o->len = 0;
}

static void out_write(out_t *o, const char *s, size_t n)
{
    while (n) {
        size_t chunk = n < sizeof o->buf - o->len ? n : sizeof o->buf - o->len;
        memcpy(o->buf + o->len, s, chunk);
        o->len += chunk;
        s += chunk;
        n -= chunk;
        if (o->len == sizeof o->buf) out_flush(o);
    }
}

static void out_printf(out_t *o, const char *fmt, ...)
{
    char text[64];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    out_write(o, text, n < (int)sizeof text ? n : sizeof text - 1);
}

static void out_json_string(out_t *o, const char *s)
{
    out_write(o, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            char e[2] = { '\\', *s };
            out_write(o, e, 2);
        } else if ((unsigned char)*s < 0x20) {
            out_printf(o, "\\u%04x", *s);
        } else {
            out_write(o, s, 1);
        }
    }
    out_write(o, "\"", 1);
}

static esp_err_t api_status(httpd_req_t *req)
{
    char query[32], value[12];
    uint32_t since = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK &&
        httpd_query_key_value(query, "since", value, sizeof value) == ESP_OK) {
        since = strtoul(value, NULL, 10);
    }
    uint32_t next = logbuf_next();
    if (since > next) since = 0;   // the page is from before a restart: send what's kept

    static out_t o;   // static: big, and the server handles one request at a time
    o.req = req;
    o.len = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (clock_valid()) {
        struct tm lt;
        clock_local(&lt);
        out_printf(&o, "{\"seconds\":%d", (lt.tm_hour * 60 + lt.tm_min) * 60 + lt.tm_sec);
    } else {
        out_printf(&o, "{\"seconds\":null");
    }
    int dir = motor_dir();
    out_printf(&o, ",\"dir\":\"%s\",\"remaining_ms\":%lu,\"total_ms\":%lu", dir > 0 ? "up" : dir < 0 ? "down" : "",
               (unsigned long)motor_remaining_ms(), (unsigned long)motor_total_ms());

    alarm_t list[MAX_ALARMS];
    int n = alarms_list(list);
    out_printf(&o, ",\"max_alarms\":%d,\"alarms\":[", MAX_ALARMS);
    for (int i = 0; i < n; i++) {
        out_printf(&o, "%s{\"minute\":%d,\"dir\":\"%s\"}", i ? "," : "", list[i].minute, list[i].dir > 0 ? "up" : "down");
    }

    out_printf(&o, "],\"log_next\":%lu,\"log\":[", (unsigned long)next);
    bool first = true;
    for (uint32_t i = since; i < next; i++) {
        char line[LOG_LINE_LENGTH];
        if (!logbuf_line(i, line, sizeof line)) continue;   // already pushed out by newer lines
        if (!first) out_write(&o, ",", 1);
        first = false;
        out_json_string(&o, line);
    }
    out_write(&o, "]}", 2);
    out_flush(&o);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Reads a form-encoded POST body ("m=450&d=up") into buf
static bool read_body(httpd_req_t *req, char *buf, size_t size)
{
    if (req->content_len >= size) return false;
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) return false;
        got += r;
    }
    buf[got] = '\0';
    return true;
}

// d=up or d=down in the body: 1 or -1, or 0 if missing or invalid
static int body_dir(const char *body)
{
    char d[8];
    if (httpd_query_key_value(body, "d", d, sizeof d) != ESP_OK) return 0;
    return !strcmp(d, "up") ? 1 : !strcmp(d, "down") ? -1 : 0;
}

static int body_minute(const char *body)
{
    char m[8];
    if (httpd_query_key_value(body, "m", m, sizeof m) != ESP_OK) return -1;
    return atoi(m);
}

static esp_err_t done(httpd_req_t *req, bool ok)
{
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t api_move(httpd_req_t *req)
{
    char body[32];
    int dir = read_body(req, body, sizeof body) ? body_dir(body) : 0;
    if (dir) event_post(dir > 0 ? CMD_UP : CMD_DOWN, SRC_WEB);
    return done(req, dir != 0);
}

static esp_err_t api_cancel(httpd_req_t *req)
{
    event_post(CMD_CANCEL, SRC_WEB);
    return done(req, true);
}

static esp_err_t api_alarm_add(httpd_req_t *req)
{
    char body[32];
    bool ok = read_body(req, body, sizeof body) && alarms_add(body_minute(body), body_dir(body));
    return done(req, ok);
}

static esp_err_t api_alarm_remove(httpd_req_t *req)
{
    char body[32];
    // Already gone counts as done: another browser may have removed it first
    if (read_body(req, body, sizeof body)) alarms_remove(body_minute(body), body_dir(body));
    return done(req, true);
}

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;   // a phone that keeps connections open can't lock others out
    httpd_handle_t server;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't start the web server");
        return;
    }
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        httpd_uri_t u = { .uri = pages[i].uri, .method = HTTP_GET, .handler = send_page, .user_ctx = (void *)&pages[i] };
        httpd_register_uri_handler(server, &u);
    }
    const httpd_uri_t api[] = {
        { .uri = "/api/status", .method = HTTP_GET, .handler = api_status },
        { .uri = "/api/move", .method = HTTP_POST, .handler = api_move },
        { .uri = "/api/cancel", .method = HTTP_POST, .handler = api_cancel },
        { .uri = "/api/alarms/add", .method = HTTP_POST, .handler = api_alarm_add },
        { .uri = "/api/alarms/remove", .method = HTTP_POST, .handler = api_alarm_remove },
    };
    for (size_t i = 0; i < sizeof api / sizeof api[0]; i++) httpd_register_uri_handler(server, &api[i]);
}
