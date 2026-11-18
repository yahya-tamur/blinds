#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "alarms.h"
#include "clock.h"
#include "events.h"
#include "inputs.h"
#include "logbuf.h"
#include "motor.h"
#include "ota.h"
#include "stats.h"
#include "sun.h"
#include "travel.h"
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
EMBEDDED(update_html)
EMBEDDED(dropdown_js)

typedef struct {
    const char *uri, *type, *start, *end;
} page_t;

static const page_t pages[] = {
    { "/", "text/html", index_html_start, index_html_end },
    { "/moving", "text/html", moving_html_start, moving_html_end },
    { "/update", "text/html", update_html_start, update_html_end },
    { "/style.css", "text/css", style_css_start, style_css_end },
    { "/dropdown.js", "text/javascript", dropdown_js_start, dropdown_js_end },
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
    char text[160];
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
    out_printf(&o, ",\"jog\":%s,\"pos\":%d", travel_is_jog() ? "true" : "false", travel_pos());
    out_printf(&o, ",\"irms\":%.2f,\"mode\":\"%s\",\"speed_up\":%.1f,\"speed_down\":%.1f", motor_irms(),
               motor_spreadcycle() ? "spread" : "stealth", travel_speed(1), travel_speed(-1));
    uint32_t drv;
    if (travel_last_driver(&drv))
        out_printf(&o, ",\"driver\":{\"current\":%lu,\"open_a\":%s,\"open_b\":%s,\"fault\":%s}",
                   (unsigned long)(drv >> 16 & 0x1F), drv & (1u << 6) ? "true" : "false", drv & (1u << 7) ? "true" : "false",
                   drv & 0x3E ? "true" : "false");
    else out_printf(&o, ",\"driver\":null");
    int etop, ebottom;
    if (travel_get_ends(&etop, &ebottom)) out_printf(&o, ",\"ends\":{\"top\":%d,\"bottom\":%d}", etop, ebottom);
    else out_printf(&o, ",\"ends\":null");
    out_printf(&o, ",\"pos_known\":%s,\"task\":\"%s\",\"task_result\":", travel_pos_known() ? "true" : "false", travel_task());
    out_json_string(&o, travel_task_result());
    int top, bottom;
    bool has_top = travel_get_stop(STOP_TOP, &top), has_bottom = travel_get_stop(STOP_BOTTOM, &bottom);
    if (has_top) out_printf(&o, ",\"stop_top\":%d", top);
    else out_printf(&o, ",\"stop_top\":null");
    if (has_bottom) out_printf(&o, ",\"stop_bottom\":%d", bottom);
    else out_printf(&o, ",\"stop_bottom\":null");

    // Today's date and sun times, and each alarm's next time, all in the controller's local time
    time_t now = time(NULL);
    bool known = clock_valid();
    if (known) {
        struct tm lt;
        localtime_r(&now, &lt);
        out_printf(&o, ",\"today\":\"%04d-%02d-%02d\"", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    } else {
        out_printf(&o, ",\"today\":null");
    }
    float lat, lon;
    if (sun_location(&lat, &lon)) {
        out_printf(&o, ",\"location\":{\"lat\":%.1f,\"lon\":%.1f},\"sun\":", lat, lon);
        if (known) {
            for (int e = 0; e < SUN_EVENTS; e++) {
                time_t t;
                out_printf(&o, "%s\"%s\":", e ? "," : "{", sun_name(e));
                if (sun_time(now, e, &t)) {
                    struct tm st;
                    localtime_r(&t, &st);
                    out_printf(&o, "%d", st.tm_hour * 60 + st.tm_min);
                } else {
                    out_printf(&o, "null");
                }
            }
            out_write(&o, "}", 1);
        } else {
            out_printf(&o, "null");
        }
    } else {
        out_printf(&o, ",\"location\":null,\"sun\":null");
    }

    // The remote's buttons, and learn mode: whether it is waiting, and the last press it took
    remote_button_t rb[MAX_REMOTE_BUTTONS];
    int nb = remote_list(rb);
    remote_t rr[MAX_REMOTES];
    int nr = remote_remotes(rr);
    out_printf(&o, ",\"remote\":{\"max\":%d,\"max_remotes\":%d,\"remotes\":[", MAX_REMOTE_BUTTONS, MAX_REMOTES);
    for (int i = 0; i < nr; i++) {
        out_printf(&o, "%s{\"addr\":%d,\"name\":", i ? "," : "", rr[i].addr);
        out_json_string(&o, rr[i].name);
        out_write(&o, "}", 1);
    }
    out_printf(&o, "],\"buttons\":[");
    for (int i = 0; i < nb; i++)
        out_printf(&o, "%s{\"addr\":%d,\"cmd\":%d,\"action\":\"%s\"}", i ? "," : "", rb[i].addr, rb[i].cmd,
                   rb[i].action > 0 ? "up" : rb[i].action < 0 ? "down" : "stop");
    remote_capture_t cap;
    bool learning = remote_learning(&cap);
    out_printf(&o, "],\"learning\":%s,\"capture\":", learning ? "true" : "false");
    if (cap.id) out_printf(&o, "{\"id\":%lu,\"addr\":%d,\"cmd\":%d}}", (unsigned long)cap.id, cap.addr, cap.cmd);
    else out_printf(&o, "null}");

    alarm_t list[MAX_ALARMS];
    int n = alarms_list(list);
    out_printf(&o, ",\"max_alarms\":%d,\"alarms\":[", MAX_ALARMS);
    for (int i = 0; i < n; i++) {
        const alarm_t *a = &list[i];
        out_printf(&o, "%s{\"kind\":\"%s\",\"dir\":\"%s\"", i ? "," : "", alarms_kind_name(a->kind), a->dir > 0 ? "up" : "down");
        if (a->kind == ALARM_RECURRING) out_printf(&o, ",\"minute\":%d", a->minute);
        else if (a->kind == ALARM_ONCE) out_printf(&o, ",\"at\":%lld", (long long)a->at);
        else out_printf(&o, ",\"event\":\"%s\",\"offset\":%d", sun_name(a->event), a->minute);
        if (known && a->skip_at > now) {
            struct tm sk;
            time_t t = (time_t)a->skip_at;
            localtime_r(&t, &sk);
            out_printf(&o, ",\"skip\":\"%04d-%02d-%02d %02d:%02d\",\"skip_at\":%lld", sk.tm_year + 1900, sk.tm_mon + 1,
                       sk.tm_mday, sk.tm_hour, sk.tm_min, (long long)a->skip_at);
        } else {
            out_printf(&o, ",\"skip\":null,\"skip_at\":null");
        }
        time_t next;
        if (known && alarms_next_run(a, now, &next)) {
            struct tm nt;
            localtime_r(&next, &nt);
            out_printf(&o, ",\"next\":%lld,\"when\":\"%04d-%02d-%02d %02d:%02d\"}", (long long)next, nt.tm_year + 1900,
                       nt.tm_mon + 1, nt.tm_mday, nt.tm_hour, nt.tm_min);
        } else {
            out_printf(&o, ",\"next\":null,\"when\":null}");
        }
    }

    health_t st;
    health_get(&st);
    out_printf(&o, "],\"stats\":{\"uptime_s\":%lu,\"heap\":%lu", (unsigned long)st.uptime_s, (unsigned long)st.heap_free);
    out_printf(&o, ",\"heap_min\":%lu,\"heap_largest\":%lu", (unsigned long)st.heap_min, (unsigned long)st.heap_largest);
    if (st.wifi) out_printf(&o, ",\"rssi\":%d", st.rssi);
    else out_printf(&o, ",\"rssi\":null");
    out_printf(&o, ",\"updating\":%s,\"reset\":", ota_busy() ? "true" : "false");
    out_json_string(&o, st.reset);
    out_write(&o, ",\"before_reset\":", 16);
    out_json_string(&o, motor_breadcrumb());
    out_write(&o, ",\"version\":", 11);
    out_json_string(&o, st.version);
    out_write(&o, "}", 1);

    out_printf(&o, ",\"log_next\":%lu,\"log\":[", (unsigned long)next);
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

// An alarm from the body (see web.h): k=recurring|once|sun (recurring if missing), d=up|down, and
// recurring: m=<minutes since midnight>; once: at=<Unix time>, or date=YYYY-MM-DD&m=<minutes> in local time;
// sun: e=dawn|sunrise|sunset|dusk&o=<offset minutes>. False if a field is missing or unreadable.
static bool body_alarm(const char *body, alarm_t *a)
{
    char v[16];
    memset(a, 0, sizeof *a);
    a->dir = body_dir(body);
    a->kind = ALARM_RECURRING;
    if (httpd_query_key_value(body, "k", v, sizeof v) == ESP_OK) {
        a->kind = !strcmp(v, "recurring") ? ALARM_RECURRING : !strcmp(v, "once") ? ALARM_ONCE : !strcmp(v, "sun") ? ALARM_SUN : -1;
    }
    bool has_m = httpd_query_key_value(body, "m", v, sizeof v) == ESP_OK;
    int m = has_m ? atoi(v) : -1;
    if (a->kind == ALARM_RECURRING) {
        a->minute = m;
        return has_m;
    }
    if (a->kind == ALARM_ONCE) {
        if (httpd_query_key_value(body, "at", v, sizeof v) == ESP_OK) {
            a->at = strtoll(v, NULL, 10);
            return true;
        }
        int y, mo, d;
        if (!has_m || m < 0 || m >= 24 * 60 || httpd_query_key_value(body, "date", v, sizeof v) != ESP_OK ||
            sscanf(v, "%d-%d-%d", &y, &mo, &d) != 3) return false;
        struct tm lt = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = m / 60, .tm_min = m % 60, .tm_isdst = -1 };
        a->at = mktime(&lt);
        return a->at > 0;
    }
    if (a->kind == ALARM_SUN) {
        if (httpd_query_key_value(body, "e", v, sizeof v) != ESP_OK) return false;
        a->event = sun_from_name(v);
        a->minute = httpd_query_key_value(body, "o", v, sizeof v) == ESP_OK ? atoi(v) : 0;
        return a->event >= 0;
    }
    return false;
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

// d=up|down&n=<Hall counts, 1..48>: a short move for setting the stops
static esp_err_t api_jog(httpd_req_t *req)
{
    char body[32], n[8];
    int dir = 0, counts = 0;
    if (read_body(req, body, sizeof body)) {
        dir = body_dir(body);
        if (httpd_query_key_value(body, "n", n, sizeof n) == ESP_OK) counts = atoi(n);
    }
    bool ok = dir && counts >= 1 && counts <= 48;
    if (ok) event_post_arg(dir > 0 ? CMD_JOG_UP : CMD_JOG_DOWN, SRC_WEB, counts);
    return done(req, ok);
}

// i=<run current, A>&m=spread|stealth (either optional): motor settings, saved
static esp_err_t api_motor_settings(httpd_req_t *req)
{
    char body[48], v[12];
    if (!read_body(req, body, sizeof body)) return done(req, false);
    float irms = -1;
    int spread = -1;
    if (httpd_query_key_value(body, "i", v, sizeof v) == ESP_OK) irms = strtof(v, NULL);
    if (httpd_query_key_value(body, "m", v, sizeof v) == ESP_OK) spread = !strcmp(v, "spread") ? 1 : !strcmp(v, "stealth") ? 0 : -1;
    motor_set(irms, spread);
    return done(req, true);
}

// d=up|down&v=<rev/s>: the run speed for that direction, saved
static esp_err_t api_speed(httpd_req_t *req)
{
    char body[32], v[12];
    int dir = 0;
    float rps = 0;
    if (read_body(req, body, sizeof body)) {
        dir = body_dir(body);
        if (httpd_query_key_value(body, "v", v, sizeof v) == ESP_OK) rps = strtof(v, NULL);
    }
    bool ok = dir && rps >= 0.1f && rps <= 8.0f;
    if (ok) travel_set_speed(dir, rps);
    return done(req, ok);
}

// w=top|bottom: STOP_TOP or STOP_BOTTOM, or -1
static int body_which(const char *body)
{
    char w[8];
    if (httpd_query_key_value(body, "w", w, sizeof w) != ESP_OK) return -1;
    return !strcmp(w, "top") ? STOP_TOP : !strcmp(w, "bottom") ? STOP_BOTTOM : -1;
}

static esp_err_t api_stop_set(httpd_req_t *req)
{
    char body[32];
    int w = read_body(req, body, sizeof body) ? body_which(body) : -1;
    if (w >= 0) event_post_arg(CMD_SET_STOP, SRC_WEB, w);
    return done(req, w >= 0);
}

static esp_err_t api_stop_clear(httpd_req_t *req)
{
    char body[32];
    int w = read_body(req, body, sizeof body) ? body_which(body) : -1;
    if (w >= 0) event_post_arg(CMD_CLEAR_STOP, SRC_WEB, w);
    return done(req, w >= 0);
}

static esp_err_t api_alarm_add(httpd_req_t *req)
{
    char body[96];
    alarm_t a;
    bool ok = read_body(req, body, sizeof body) && body_alarm(body, &a) && alarms_add(&a);
    return done(req, ok);
}

// The same fields as remove, plus skip=1 (skip its next time once) or skip=0 (don't)
static esp_err_t api_alarm_skip(httpd_req_t *req)
{
    char body[112], v[4];
    alarm_t a;
    bool ok = read_body(req, body, sizeof body) && body_alarm(body, &a) &&
              httpd_query_key_value(body, "skip", v, sizeof v) == ESP_OK && alarms_skip(&a, !strcmp(v, "1"));
    return done(req, ok);
}

static esp_err_t api_alarm_remove(httpd_req_t *req)
{
    char body[96];
    alarm_t a;
    // Already gone counts as done: another browser may have removed it first
    if (read_body(req, body, sizeof body) && body_alarm(body, &a)) alarms_remove(&a);
    return done(req, true);
}

// lat=<degrees north>&lon=<degrees east>, kept to one decimal place; clear=1 forgets it
static esp_err_t api_location(httpd_req_t *req)
{
    char body[48], lat[12], lon[12];
    if (!read_body(req, body, sizeof body)) return done(req, false);
    if (httpd_query_key_value(body, "clear", lat, sizeof lat) == ESP_OK) {
        sun_clear_location();
        return done(req, true);
    }
    bool ok = httpd_query_key_value(body, "lat", lat, sizeof lat) == ESP_OK &&
              httpd_query_key_value(body, "lon", lon, sizeof lon) == ESP_OK &&
              sun_set_location(strtof(lat, NULL), strtof(lon, NULL));
    return done(req, ok);
}

static esp_err_t api_ends_detect(httpd_req_t *req)
{
    event_post(CMD_DETECT_ENDS, SRC_WEB);
    return done(req, true);
}

static esp_err_t api_ends_home(httpd_req_t *req)
{
    int top;
    bool ok = travel_get_ends(&top, NULL);
    if (ok) event_post(CMD_HOME, SRC_WEB);
    return done(req, ok);
}

// A code from the body: 0..255, decimal or 0x hex; -1 if missing or out of range
static int body_code(const char *body, const char *key)
{
    char v[8], *end;
    if (httpd_query_key_value(body, key, v, sizeof v) != ESP_OK || !v[0]) return -1;
    long n = strtol(v, &end, 0);
    return *end || n < 0 || n > 255 ? -1 : (int)n;
}

// on=1 starts (or renews) learn mode, on=0 ends it. Answers {"id":n}: the capture for this request carries it.
static esp_err_t api_remote_learn(httpd_req_t *req)
{
    char body[16], v[4];
    if (!read_body(req, body, sizeof body) || httpd_query_key_value(body, "on", v, sizeof v) != ESP_OK) return done(req, false);
    char json[24];
    snprintf(json, sizeof json, "{\"id\":%lu}", (unsigned long)remote_learn(!strcmp(v, "1")));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

// a=<address>&c=<command>&do=up|down|stop
static esp_err_t api_remote_add(httpd_req_t *req)
{
    char body[48], v[8];
    if (!read_body(req, body, sizeof body)) return done(req, false);
    int a = body_code(body, "a"), c = body_code(body, "c");
    int action = httpd_query_key_value(body, "do", v, sizeof v) != ESP_OK ? 2 :
                 !strcmp(v, "up") ? 1 : !strcmp(v, "down") ? -1 : !strcmp(v, "stop") ? 0 : 2;
    return done(req, a >= 0 && c >= 0 && action != 2 && remote_add(a, c, action));
}

// a=<address>&c=<command>
static esp_err_t api_remote_remove(httpd_req_t *req)
{
    char body[32];
    if (!read_body(req, body, sizeof body)) return done(req, false);
    int a = body_code(body, "a"), c = body_code(body, "c");
    if (a >= 0 && c >= 0) remote_remove(a, c);
    return done(req, a >= 0 && c >= 0);
}

// a=<address>: a new remote, named "Remote N"
static esp_err_t api_remote_new(httpd_req_t *req)
{
    char body[16];
    int a = read_body(req, body, sizeof body) ? body_code(body, "a") : -1;
    return done(req, a >= 0 && remote_new(a));
}

// Decodes a form value in place: + is a space, %XX a byte
static void url_decode(char *s)
{
    char *out = s;
    for (; *s; s++) {
        if (*s == '+') *out++ = ' ';
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else *out++ = *s;
    }
    *out = '\0';
}

// a=<address>&name=<new name> (UTF-8, cut to fit at a whole character; spaces at the ends dropped)
static esp_err_t api_remote_rename(httpd_req_t *req)
{
    char body[160], name[120];
    if (!read_body(req, body, sizeof body)) return done(req, false);
    int a = body_code(body, "a");
    if (a < 0 || httpd_query_key_value(body, "name", name, sizeof name) != ESP_OK) return done(req, false);
    url_decode(name);
    char *start = name;
    while (*start == ' ') start++;
    size_t n = strlen(start);
    if (n > REMOTE_NAME_LENGTH - 1) {
        n = REMOTE_NAME_LENGTH - 1;
        while (n && ((unsigned char)start[n] & 0xC0) == 0x80) n--;   // not inside a UTF-8 character
    }
    while (n && start[n - 1] == ' ') n--;
    start[n] = '\0';
    for (char *c = start; *c; c++) if ((unsigned char)*c < 0x20) *c = ' ';
    return done(req, remote_rename(a, start));
}

// a=<address>: the remote and all its buttons
static esp_err_t api_remote_delete(httpd_req_t *req)
{
    char body[16];
    int a = read_body(req, body, sizeof body) ? body_code(body, "a") : -1;
    if (a >= 0) remote_delete(a);
    return done(req, a >= 0);
}

static httpd_handle_t server;

httpd_handle_t web_server(void) { return server; }

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 36;
    cfg.stack_size = 6144;         // the update handler needs more than the default 4 KB
    cfg.lru_purge_enable = true;   // a phone that keeps connections open can't lock others out
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
        { .uri = "/api/alarms/skip", .method = HTTP_POST, .handler = api_alarm_skip },
        { .uri = "/api/location", .method = HTTP_POST, .handler = api_location },
        { .uri = "/api/jog", .method = HTTP_POST, .handler = api_jog },
        { .uri = "/api/motor/settings", .method = HTTP_POST, .handler = api_motor_settings },
        { .uri = "/api/speed", .method = HTTP_POST, .handler = api_speed },
        { .uri = "/api/stops/set", .method = HTTP_POST, .handler = api_stop_set },
        { .uri = "/api/stops/clear", .method = HTTP_POST, .handler = api_stop_clear },
        { .uri = "/api/ends/detect", .method = HTTP_POST, .handler = api_ends_detect },
        { .uri = "/api/ends/home", .method = HTTP_POST, .handler = api_ends_home },
        { .uri = "/api/remote/learn", .method = HTTP_POST, .handler = api_remote_learn },
        { .uri = "/api/remote/add", .method = HTTP_POST, .handler = api_remote_add },
        { .uri = "/api/remote/remove", .method = HTTP_POST, .handler = api_remote_remove },
        { .uri = "/api/remote/new", .method = HTTP_POST, .handler = api_remote_new },
        { .uri = "/api/remote/rename", .method = HTTP_POST, .handler = api_remote_rename },
        { .uri = "/api/remote/delete", .method = HTTP_POST, .handler = api_remote_delete },
    };
    for (size_t i = 0; i < sizeof api / sizeof api[0]; i++) httpd_register_uri_handler(server, &api[i]);
    ota_register(server);
}
