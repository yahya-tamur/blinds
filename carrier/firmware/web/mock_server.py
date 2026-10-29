#!/usr/bin/env python3
"""A stand-in for the controller's web server, for trying the pages on a computer.

    python3 web/mock_server.py [port]      (default port 8766)

then open http://127.0.0.1:8766/. It serves the files in web/ as they are (edits show up on
reload) and answers the same API as main/web.c, keeping alarms and the motor state in memory:

  - the clock is this computer's local time
  - Up / Down "run the motor" for 65.5 s, like the simulated motor (nothing moves)
  - Stop and the alarms work, and alarms go off at their minute like on the board
  - what the board would log goes to the browser console via /api/status
"""

import json
import sys
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

WEB_DIR = Path(__file__).resolve().parent
PAGES = {
    "/": ("index.html", "text/html"),
    "/moving": ("moving.html", "text/html"),
    "/style.css": ("style.css", "text/css"),
    "/favicon.svg": ("favicon.svg", "image/svg+xml"),
    "/favicon.ico": ("favicon.svg", "image/svg+xml"),
}
RUN_MS = 65500
MAX_ALARMS = 20
LOG_LINES = 32

lock = threading.Lock()
alarms = []           # [(minute since midnight, 1 for up / -1 for down)], sorted
run = None            # (dir, start time.monotonic()) while the "motor" is moving
last_minute = None
log_lines = []        # the last LOG_LINES lines, like logbuf.c
log_next = 0          # number the next line gets


def log(line):
    global log_next
    print(line, flush=True)
    log_lines.append(line)
    del log_lines[:-LOG_LINES]
    log_next += 1


def remaining_ms():
    global run
    if run is not None and (time.monotonic() - run[1]) * 1000 >= RUN_MS:
        run = None
    return 0 if run is None else max(0, RUN_MS - int((time.monotonic() - run[1]) * 1000))


def command(cmd, source):
    """The control task's rules: up/down start a run unless one is going, cancel ends it."""
    global run
    if cmd == "cancel":
        if remaining_ms():
            run = None
            log(f"I control: Cancelled by {source}")
    elif not remaining_ms():
        log(f"I control: {cmd.capitalize()} from {source}")
        run = (1 if cmd == "up" else -1, time.monotonic())


def check_alarms():
    global last_minute
    while True:
        now = datetime.now()
        minute = now.hour * 60 + now.minute
        with lock:
            if minute != last_minute:
                last_minute = minute
                for m, d in alarms:
                    if m == minute:
                        log(f"I alarms: Alarm: {'up' if d > 0 else 'down'}")
                        command("up" if d > 0 else "down", "alarm")
                        break
        time.sleep(1)


class Handler(BaseHTTPRequestHandler):
    def send(self, code, body=b"", content_type=None):
        self.send_response(code)
        if content_type:
            self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store" if content_type == "application/json" else "no-cache")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in PAGES:
            name, content_type = PAGES[url.path]
            self.send(200, (WEB_DIR / name).read_bytes(), content_type)
        elif url.path == "/api/status":
            since = int(parse_qs(url.query).get("since", ["0"])[0])
            with lock:
                if since > log_next:
                    since = 0
                now = datetime.now()
                ms = remaining_ms()
                status = {
                    "seconds": now.hour * 3600 + now.minute * 60 + now.second,
                    "dir": "" if not ms else "up" if run[0] > 0 else "down",
                    "remaining_ms": ms,
                    "total_ms": RUN_MS,
                    "max_alarms": MAX_ALARMS,
                    "alarms": [{"minute": m, "dir": "up" if d > 0 else "down"} for m, d in alarms],
                    "log_next": log_next,
                    "log": log_lines[max(0, len(log_lines) - (log_next - since)):],
                }
            self.send(200, json.dumps(status).encode(), "application/json")
        else:
            self.send(404)

    def do_POST(self):
        body = parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode())
        field = lambda name: body.get(name, [""])[0]
        d = {"up": 1, "down": -1}.get(field("d"), 0)
        ok = True
        with lock:
            if self.path == "/api/move" and d:
                command(field("d"), "web page")
            elif self.path == "/api/cancel":
                command("cancel", "web page")
            elif self.path == "/api/alarms/add":
                m = int(field("m") or -1)
                ok = d != 0 and 0 <= m < 1440 and len(alarms) < MAX_ALARMS
                if ok:
                    alarms.append((m, d))
                    alarms.sort()
                    log(f"I alarms: Added {m // 60:02d}:{m % 60:02d} {field('d')}")
            elif self.path == "/api/alarms/remove":
                m = int(field("m") or -1)
                if (m, d) in alarms:
                    alarms.remove((m, d))
                    log(f"I alarms: Removed {m // 60:02d}:{m % 60:02d} {field('d')}")
            else:
                ok = False
        self.send(204 if ok else 400)

    def log_message(self, *args):
        pass  # keep the terminal for the controller's own log lines


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
    threading.Thread(target=check_alarms, daemon=True).start()
    log("I web: Mock controller started")
    print(f"http://127.0.0.1:{port}/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
