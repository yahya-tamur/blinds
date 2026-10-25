#include "log.h"
#include "WiFiS3.h"
#include "clock.h"
#include "alarms.h"
#include "pages.h"
#include "wifi.h"
#include "poll.h"
#include "arduino_secrets.h"

const char ssid[] = SECRET_SSID;
const char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;

WiFiServer server(80);

static void sync_clock() {
  // WiFi.getTime() returns 0 until the module has reached an NTP server,
  // which can take a few seconds after connecting.
  unsigned long epoch = 0;
  for (int tries = 0; tries < 20 && epoch == 0; tries++) {
    epoch = WiFi.getTime();
    if (epoch == 0) {
      delay(1000);
    }
  }

  if (epoch > 0) {
    set_clock(epoch);
    Log.println("RTC synced with internet time.");
  } else {
    Log.println("Failed to get time from server, keeping RTC time.");
  }
}

void initialize_wifi() {

  while (status != WL_CONNECTED) {
    Log.print("Attempting to connect to SSID: ");
    Log.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(5000); // Wait 5 seconds for connection
  }

  sync_clock();

  // Start the HTTP server
  server.begin();

  // Print your local IP address
  Log.println("\n--- Connected to Wi-Fi ---");
  Log.print("IP Address: ");
  Log.println(WiFi.localIP());
}

// Copies the value of `name` from the query string of `path` into `out`.
// Returns false (and leaves `out` empty) if it isn't there.
static bool get_param(const char* path, const char* name, char* out, size_t size) {
  size_t name_len = strlen(name);
  const char* p = strchr(path, '?');
  while (p) {
    p++;
    const char* end = strchr(p, '&');
    if (!end) {
      end = p + strlen(p);
    }
    if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
      const char* value = p + name_len + 1;
      size_t n = end - value;
      if (n >= size) {
        n = size - 1;
      }
      memcpy(out, value, n);
      out[n] = '\0';
      return true;
    }
    p = *end ? end : nullptr;
  }
  out[0] = '\0';
  return false;
}

// Parses "HH:MM" (the colon may arrive URL-encoded as %3A) into minutes since midnight, or -1.
static int parse_time(const char* t) {
  const char* sep = strchr(t, ':');
  int sep_len = 1;
  if (!sep) {
    sep = strstr(t, "%3A");
    if (!sep) {
      sep = strstr(t, "%3a");
    }
    sep_len = 3;
  }
  if (!sep || sep == t) {
    return -1;
  }
  int hours = atoi(t);
  int minutes = atoi(sep + sep_len);
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    return -1;
  }
  return hours * 60 + minutes;
}

// Formats seconds since midnight as "2:20:23 AM", or "2:20 AM" without seconds.
static void format_time(char* buf, size_t size, long seconds, bool with_seconds) {
  int hours = seconds / 3600;
  int minutes = seconds / 60 % 60;
  const char* ampm = hours < 12 ? "AM" : "PM";
  int hours12 = hours % 12 == 0 ? 12 : hours % 12;
  if (with_seconds) {
    snprintf(buf, size, "%d:%02d:%02d %s", hours12, minutes, (int)(seconds % 60), ampm);
  } else {
    snprintf(buf, size, "%d:%02d %s", hours12, minutes, ampm);
  }
}

// What the motor is doing, for the /moving page
static int motor_dir = 0;
static unsigned long motor_start = 0;
static unsigned long motor_ms = 0;

void set_motor_state(int dir, unsigned long ms) {
  motor_dir = dir;
  motor_start = millis();
  motor_ms = ms;
}

static unsigned long motor_remaining_ms() {
  unsigned long elapsed = millis() - motor_start;
  return motor_dir == 0 || elapsed >= motor_ms ? 0 : motor_ms - elapsed;
}

// Response output goes through this fixed buffer, so it reaches the WiFi module
// in a few large writes instead of one slow write per piece.
static WiFiClient* out_client;
static char out_buf[512];
static size_t out_len = 0;

static void out_begin(WiFiClient& client) {
  out_client = &client;
  out_len = 0;
}

static void out_flush() {
  if (out_len > 0) {
    out_client->write((const uint8_t*)out_buf, out_len);
    out_len = 0;
  }
}

static void out_write(const char* s, size_t n) {
  while (n > 0) {
    size_t chunk = min(n, sizeof(out_buf) - out_len);
    memcpy(out_buf + out_len, s, chunk);
    out_len += chunk;
    s += chunk;
    n -= chunk;
    if (out_len == sizeof(out_buf)) {
      out_flush();
    }
  }
}

static void out_print(const char* s) {
  out_write(s, strlen(s));
}

static void out_print(unsigned long n) {
  static char buf[12];  // static to keep it off the stack, like the buffers above poll_wifi()
  snprintf(buf, sizeof(buf), "%lu", n);
  out_print(buf);
}

static void out_headers(const char* content_type) {
  out_print("HTTP/1.1 200 OK\r\nContent-type:");
  out_print(content_type);
  out_print("\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
}

// True if the placeholder name (not null-terminated) is `want`.
static bool is(const char* name, size_t len, const char* want) {
  return strlen(want) == len && strncmp(name, want, len) == 0;
}

// Buffers for handling a request. They're static rather than local because the
// stack only has 1 KB set aside, and answering a request is the deepest the
// program goes (see memstats.cpp). Requests are handled one at a time, so
// sharing them is safe.
static char request[160];  // the request line, e.g. "GET /add?t=07%3A30&d=up HTTP/1.1"
static char param[16];     // one query value at a time, e.g. "07%3A30"
static char text[16];      // one formatted time at a time, e.g. "12:59:59 PM"

// The pages in pages.h have {{NAME}} placeholders. Each send_ function below
// walks its page with the same loop: copy the text up to the next "{{", fill
// in the value for that name, carry on after the "}}".

static void send_index(WiFiClient& client) {
  long seconds = get_second();
  out_begin(client);
  out_headers("text/html");

  const char* p = INDEX_HTML;
  while (const char* open = strstr(p, "{{")) {
    const char* name = open + 2;
    const char* close = strstr(name, "}}");
    if (!close) {
      break;
    }
    size_t len = close - name;
    out_write(p, open - p);
    p = close + 2;

    if (is(name, len, "ALARM_ROW")) {
      // One copy of ALARM_ROW_HTML per alarm, walked the same way
      for (int i = 0; i < alarm_count(); i++) {
        Alarm a = get_alarm(i);
        const char* r = ALARM_ROW_HTML;
        while (const char* row_open = strstr(r, "{{")) {
          const char* row_name = row_open + 2;
          const char* row_close = strstr(row_name, "}}");
          if (!row_close) {
            break;
          }
          size_t row_len = row_close - row_name;
          out_write(r, row_open - r);
          r = row_close + 2;

          if (is(row_name, row_len, "TIME")) {
            format_time(text, sizeof(text), a.minute * 60L, false);
            out_print(text);
          } else if (is(row_name, row_len, "DIR")) {
            out_print(a.dir == ALARM_UP ? "up" : "down");
          } else if (is(row_name, row_len, "INDEX")) {
            out_print((unsigned long)i);
          } else if (is(row_name, row_len, "MINUTE")) {
            out_print((unsigned long)a.minute);
          }
        }
        out_print(r);
      }
    } else if (is(name, len, "ALARM_EMPTY")) {
      if (alarm_count() == 0) {
        out_print(ALARM_EMPTY_HTML);
      }
    } else if (is(name, len, "FORM_DISABLED")) {
      if (alarm_count() >= MAX_ALARMS) {
        out_print("disabled");
      }
    } else if (is(name, len, "NOW")) {
      format_time(text, sizeof(text), seconds, true);
      out_print(text);
    } else if (is(name, len, "SECONDS")) {
      out_print((unsigned long)seconds);
    }
  }
  out_print(p);
  out_flush();
}

static void send_redirect(WiFiClient& client, const char* location) {
  // Redirect so refreshing the page doesn't repeat the action
  client.println("HTTP/1.1 303 See Other");
  client.print("Location: ");
  client.println(location);
  client.println("Connection: close");
  client.println();
}

static void send_not_found(WiFiClient& client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Connection: close");
  client.println();
}

static void send_moving(WiFiClient& client) {
  unsigned long remaining = motor_remaining_ms();
  if (remaining == 0) {
    send_redirect(client, "/");
    return;
  }
  out_begin(client);
  out_headers("text/html");

  const char* p = MOVING_HTML;
  while (const char* open = strstr(p, "{{")) {
    const char* name = open + 2;
    const char* close = strstr(name, "}}");
    if (!close) {
      break;
    }
    size_t len = close - name;
    out_write(p, open - p);
    p = close + 2;

    if (is(name, len, "DIR")) {
      out_print(motor_dir > 0 ? "up" : "down");
    } else if (is(name, len, "TOTAL_MS")) {
      out_print(motor_ms);
    } else if (is(name, len, "REMAINING_MS")) {
      out_print(remaining);
    }
  }
  out_print(p);
  out_flush();
}

// For /style.css and /favicon.svg. The pages link to them as name?v=<hash of the
// contents>, so browsers may keep them for good: an edited file gets a new
// address (see web/build_pages.py).
static void send_cached_file(WiFiClient& client, const char* content_type, const char* body) {
  out_begin(client);
  out_print("HTTP/1.1 200 OK\r\nContent-type:");
  out_print(content_type);
  out_print("\r\nCache-Control: public, max-age=31536000, immutable\r\nConnection: close\r\n\r\n");
  out_print(body);
  out_flush();
}

// True if path is `name`, with or without a query string after it.
static bool is_path(const char* path, const char* name) {
  size_t len = strlen(name);
  return strncmp(path, name, len) == 0 && (path[len] == '\0' || path[len] == '?');
}

// Polled every second by both pages: the main page uses it to keep its clock
// live and to switch to /moving when a run starts; /moving uses it to notice
// when the run ends or is cancelled elsewhere. Both print "log" to the browser's
// console: the Serial Monitor lines numbered `since` onwards (see log.h).
static void send_status(WiFiClient& client, const char* path) {
  unsigned long since = get_param(path, "since", param, sizeof(param)) ? strtoul(param, nullptr, 10) : 0;
  unsigned long next = log_next();
  if (since > next) {
    since = 0;  // the page is from before a restart; send what's kept
  }

  out_begin(client);
  out_headers("application/json");
  out_print("{\"seconds\":");
  out_print((unsigned long)get_second());
  out_print(",\"remaining_ms\":");
  out_print(motor_remaining_ms());
  out_print(",\"log_next\":");
  out_print(next);
  out_print(",\"log\":[");
  bool first = true;
  for (unsigned long n = since; n < next; n++) {
    const char* line = log_line(n);
    if (!line) {
      continue;  // already pushed out by newer lines
    }
    if (!first) {
      out_print(",");
    }
    first = false;

    // The line as a JSON string
    out_print("\"");
    for (const char* c = line; *c; c++) {
      if (*c == '"' || *c == '\\') {
        char escaped[2] = {'\\', *c};
        out_write(escaped, 2);
      } else if ((unsigned char)*c < 0x20) {
        snprintf(text, sizeof(text), "\\u%04x", *c);
        out_print(text);
      } else {
        out_write(c, 1);
      }
    }
    out_print("\"");
  }
  out_print("]}");
  out_flush();
}

int poll_wifi() {
  WiFiClient client = server.available();
  int ans = 0;

  if (client) {
    size_t request_len = 0;
    size_t line_len = 0;
    bool got_request = false;
    unsigned long start = millis();

    // Read the request line, then the headers up to the blank line that ends them
    while (client.connected() && millis() - start < 2000) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (!got_request) {
            got_request = true;
          } else if (line_len == 0) {
            break;
          }
          line_len = 0;
        } else if (c != '\r') {
          if (!got_request && request_len < sizeof(request) - 1) {
            request[request_len++] = c;
          }
          line_len++;
        }
      }
    }
    request[request_len] = '\0';

    // Cut the path out from between the first two spaces
    char* path = strchr(request, ' ');
    path = path ? path + 1 : request + request_len;
    char* path_end = strchr(path, ' ');
    if (path_end) {
      *path_end = '\0';
    }

    if (strcmp(path, "/") == 0) {
      send_index(client);
    } else if (strcmp(path, "/moving") == 0) {
      send_moving(client);
    } else if (is_path(path, "/style.css")) {
      send_cached_file(client, "text/css", STYLE_CSS);
    } else if (is_path(path, "/favicon.svg") || is_path(path, "/favicon.ico")) {
      // Some browsers ask for /favicon.ico regardless of the page's icon link
      send_cached_file(client, "image/svg+xml", FAVICON_SVG);
    } else if (is_path(path, "/status")) {
      send_status(client, path);
    } else if (strcmp(path, "/cancel") == 0) {
      ans = CMD_CANCEL;
      send_redirect(client, "/");
    } else if (strcmp(path, "/H") == 0) {
      ans = 1;
      send_redirect(client, "/moving");
    } else if (strcmp(path, "/L") == 0) {
      ans = -1;
      send_redirect(client, "/moving");
    } else if (strncmp(path, "/add?", 5) == 0) {
      get_param(path, "d", param, sizeof(param));
      int dir = strcmp(param, "up") == 0 ? ALARM_UP : strcmp(param, "down") == 0 ? ALARM_DOWN : 0;
      get_param(path, "t", param, sizeof(param));
      add_alarm(parse_time(param), dir);
      send_redirect(client, "/");
    } else if (strncmp(path, "/del?", 5) == 0) {
      if (get_param(path, "i", param, sizeof(param)) && param[0] != '\0') {
        remove_alarm(atoi(param));
      }
      send_redirect(client, "/");
    } else {
      send_not_found(client);
    }

    // Close the connection
    delay(10); // Give the browser time to receive the data
    client.stop();
  }
  return ans;

}