#!/usr/bin/env python3
"""A stand-in for the Arduino's web server, for trying the pages on a computer.

    python3 web/mock_server.py [port]      (default port 8766)

then open http://127.0.0.1:8766/. It answers the same addresses as wifi.cpp and
fills the templates the same way, keeping alarms and the motor state in memory:

  - the clock is this computer's local time
  - Up / Down "run the motor" for 65.5 s, like a full run with its planned stops (nothing moves; /moving counts down)
  - Stop and the alarm list work, and alarms go off at their minute like on the board
  - what it would print to the Serial Monitor goes to the browser console via /status,
    like log.cpp (there's no memory readout, since there's no board to measure)

Templates are re-read on every request, so edits in web/ show up on reload without
running build_pages.py. Nothing here is used by the sketch.
"""

import json
import re
import sys
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from build_pages import all_templates

RUN_MS = 65500
MAX_ALARMS = 20

lock = threading.Lock()
alarms = []           # [(minute since midnight, 1 for up / -1 for down)], sorted
run = None            # (dir, start time.monotonic(), ms) while the "motor" is moving
last_minute = None
LOG_LINES = 16
log_lines = []        # the last LOG_LINES lines, like log.cpp
log_next = 0          # number the next line gets


def log(line):
    global log_next
    print(line, flush=True)
    log_lines.append(line)
    del log_lines[:-LOG_LINES]
    log_next += 1


def seconds_now():
    now = datetime.now()
    return now.hour * 3600 + now.minute * 60 + now.second


def remaining_ms():
    if run is None:
        return 0
    direction, start, ms = run
    return max(0, ms - int((time.monotonic() - start) * 1000))


def format_time(seconds, with_seconds):
    hours, minutes = seconds // 3600, seconds // 60 % 60
    hours12 = hours % 12 or 12
    ampm = "AM" if hours < 12 else "PM"
    if with_seconds:
        return f"{hours12}:{minutes:02d}:{seconds % 60:02d} {ampm}"
    return f"{hours12}:{minutes:02d} {ampm}"


def start_run(direction):
    global run
    if remaining_ms() == 0:
        run = (direction, time.monotonic(), RUN_MS)


def check_alarms():
    """Like check_alarms() in alarms.cpp: fires once when an alarm's minute arrives."""
    global last_minute
    minute = seconds_now() // 60
    if minute == last_minute:
        return
    last_minute = minute
    for alarm_minute, direction in alarms:
        if alarm_minute == minute:
            log(f"Alarm: {'up' if direction > 0 else 'down'}")
            start_run(direction)
            return


def parse_time(t):
    match = re.fullmatch(r"(\d{1,2}):(\d{1,2})", t)
    if not match:
        return None
    hours, minutes = int(match[1]), int(match[2])
    return hours * 60 + minutes if hours < 24 and minutes < 60 else None


def fill(template, values):
    return re.sub(r"\{\{(\w+)\}\}", lambda m: str(values.get(m[1], "")), template)


def index_page():
    t = all_templates()
    rows = "".join(
        fill(t["ALARM_ROW_HTML"], {
            "TIME": format_time(minute * 60, False),
            "DIR": "up" if direction > 0 else "down",
            "INDEX": i,
            "MINUTE": minute,
        })
        for i, (minute, direction) in enumerate(alarms)
    )
    seconds = seconds_now()
    return fill(t["INDEX_HTML"], {
        "ALARM_ROW": rows,
        "ALARM_EMPTY": "" if alarms else t["ALARM_EMPTY_HTML"],
        "FORM_DISABLED": "disabled" if len(alarms) >= MAX_ALARMS else "",
        "NOW": format_time(seconds, True),
        "SECONDS": seconds,
    })


def moving_page():
    return fill(all_templates()["MOVING_HTML"], {
        "DIR": "up" if run[0] > 0 else "down",
        "TOTAL_MS": run[2],
        "REMAINING_MS": remaining_ms(),
        "NOW": format_time(seconds_now(), True),
        "SECONDS": seconds_now(),
    })


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        global run
        url = urlsplit(self.path)
        query = {k: v[0] for k, v in parse_qs(url.query).items()}
        with lock:
            check_alarms()
            if url.path == "/":
                self.send(200, "text/html", index_page())
            elif url.path == "/moving":
                if remaining_ms() > 0:
                    self.send(200, "text/html", moving_page())
                else:
                    self.redirect("/")
            elif url.path in ("/style.css", "/favicon.svg", "/favicon.ico"):
                # Some browsers ask for /favicon.ico regardless of the page's icon link
                name, content_type = {"/style.css": ("STYLE_CSS", "text/css"),
                                      "/favicon.svg": ("FAVICON_SVG", "image/svg+xml"),
                                      "/favicon.ico": ("FAVICON_SVG", "image/svg+xml")}[url.path]
                self.send(200, content_type, all_templates()[name],
                          cache="public, max-age=31536000, immutable")
            elif url.path == "/status":
                since = int(query["since"]) if query.get("since", "").isdigit() else 0
                if since > log_next:
                    since = 0
                first_kept = log_next - len(log_lines)
                lines = log_lines[max(0, since - first_kept):]
                self.send(200, "application/json", json.dumps({
                    "seconds": seconds_now(),
                    "remaining_ms": remaining_ms(),
                    "log_next": log_next,
                    "log": lines,
                }))
            elif url.path == "/cancel":
                if remaining_ms() > 0:
                    log("Cancelled from web page")
                run = None
                self.redirect("/")
            elif url.path in ("/H", "/L"):
                start_run(1 if url.path == "/H" else -1)
                self.redirect("/moving")
            elif url.path == "/add":
                minute = parse_time(query.get("t", ""))
                direction = {"up": 1, "down": -1}.get(query.get("d"))
                if minute is not None and direction and len(alarms) < MAX_ALARMS:
                    alarms.append((minute, direction))
                    alarms.sort(key=lambda a: a[0])
                self.redirect("/")
            elif url.path == "/del":
                i = query.get("i", "")
                if i.isdigit() and int(i) < len(alarms):
                    del alarms[int(i)]
                self.redirect("/")
            else:
                self.send(404, "text/plain", "Not found")

    def send(self, code, content_type, body, cache="no-store"):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-type", content_type)
        self.send_header("Cache-Control", cache)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def redirect(self, location):
        self.send_response(303)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, format, *args):
        if "/status" not in self.path:  # it's asked for every second
            super().log_message(format, *args)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
    log(f"Mock controller at http://127.0.0.1:{port}/  (Ctrl+C to stop)")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
