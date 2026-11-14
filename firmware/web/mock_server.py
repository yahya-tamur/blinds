#!/usr/bin/env python3
"""A stand-in for the controller's web server, for trying the pages on a computer.

    python3 web/mock_server.py [port]      (default port 8766)

then open http://127.0.0.1:8766/. It serves the files in web/ as they are (edits show up on
reload) and answers the same API as main/web.c, keeping alarms and the motor state in memory:

  - the clock is this computer's local time
  - Up / Down "run the motor" for 65.5 s, like the simulated motor (nothing moves)
  - Stop and the alarms work (recurring, one-time and sun), and go off at their minute like on the board
  - the location is kept in memory; /locate.html serves ../../docs/locate.html for "Use my location"
  - what the board would log goes to the browser console via /api/status
  - /api/status has made-up health stats; /update accepts a file and pretends to restart
"""

import json
import math
import sys
import threading
import time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

WEB_DIR = Path(__file__).resolve().parent
DOCS_DIR = WEB_DIR.parent.parent / "docs"
PAGES = {
    "/": ("index.html", "text/html"),
    "/moving": ("moving.html", "text/html"),
    "/style.css": ("style.css", "text/css"),
    "/dropdown.js": ("dropdown.js", "text/javascript"),
    "/favicon.svg": ("favicon.svg", "image/svg+xml"),
    "/favicon.ico": ("favicon.svg", "image/svg+xml"),
    "/update": ("update.html", "text/html"),
    "/locate.html": (DOCS_DIR / "locate.html", "text/html"),
}
STARTED = time.monotonic()    # "uptime"; an update "restarts" it
RUN_MS = 65500
MAX_ALARMS = 20
LOG_LINES = 32

lock = threading.Lock()
alarms = []           # dicts like the board's alarm_t: {"kind", "dir" (1/-1), and "minute" | "at" | "event" + "offset"}
location = None       # (lat, lon), one decimal
SUN_EVENTS = ["dawn", "sunrise", "sunset", "dusk"]
run = None            # (dir, start time.monotonic()) while the "motor" is moving
last_minute = None
log_lines = []        # the last LOG_LINES lines, like logbuf.c
log_next = 0          # number the next line gets
pos = 0               # Hall position (travel.c), counts of 30 degrees of the sprocket, up = more
stops = {"top": None, "bottom": None}
motor = {"irms": 1.7, "mode": "stealth"}
speeds = {"up": 5.0, "down": 5.0}


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
        global pos
        log(f"I control: {cmd.capitalize()} from {source}")
        stop = stops["top" if cmd == "up" else "bottom"]
        if stop is None:
            log(f"W travel: No {'top' if cmd == 'up' else 'bottom'} stop set: not running (set it under Adjust stops)")
            return
        if (stop - pos) * (1 if cmd == "up" else -1) <= 0:
            log(f"I travel: Already at or past the {'top' if cmd == 'up' else 'bottom'} stop")
            return
        pos = stop          # the mock "arrives" at once; the page still shows the run
        run = (1 if cmd == "up" else -1, time.monotonic())


def sun_time(date, event):
    """Local datetime of the event on that local date (sun.c, the same formula), or None."""
    if location is None:
        return None
    lat, lon = location
    rad = math.pi / 180
    n = (date - datetime(2000, 1, 1).date()).days
    j = n - lon / 360
    m = (357.5291 + 0.98560028 * j) % 360
    c = 1.9148 * math.sin(m * rad) + 0.02 * math.sin(2 * m * rad) + 0.0003 * math.sin(3 * m * rad)
    lam = (m + c + 180 + 102.9372) % 360
    transit = j + 0.0053 * math.sin(m * rad) - 0.0069 * math.sin(2 * lam * rad)
    sin_d = math.sin(lam * rad) * math.sin(23.4397 * rad)
    cos_d = math.sqrt(1 - sin_d * sin_d)
    h0 = -6.0 if event in ("dawn", "dusk") else -0.833
    cos_w = (math.sin(h0 * rad) - math.sin(lat * rad) * sin_d) / (math.cos(lat * rad) * cos_d)
    if not -1 <= cos_w <= 1:
        return None
    w = math.acos(cos_w) / rad / 360
    t = transit - w if event in ("dawn", "sunrise") else transit + w
    return datetime.fromtimestamp(round((t + 10957.5) * 86400))


def next_time(a, now):
    """When the alarm next goes off after now (alarms_next), or None."""
    if a["kind"] == "once":
        return datetime.fromtimestamp(a["at"])
    if a["kind"] == "recurring":
        for days in (0, 1):
            t = datetime.combine(now.date() + timedelta(days=days), datetime.min.time()) + timedelta(minutes=a["minute"])
            if t > now:
                return t
        return None
    for days in (-1, 0, 1, 2):
        t = sun_time(now.date() + timedelta(days=days), a["event"])
        if t is not None and t + timedelta(minutes=a["offset"]) > now:
            return t + timedelta(minutes=a["offset"])
    return None


def same_slot(a, b):
    """At most one alarm per slot: the same minute of the day, the same moment, the same sun event."""
    key = {"recurring": "minute", "once": "at", "sun": "event"}[a["kind"]]
    return a["kind"] == b["kind"] and a[key] == b[key]


def describe(a):
    d = "up" if a["dir"] > 0 else "down"
    if a["kind"] == "recurring":
        return f"recurring {a['minute'] // 60:02d}:{a['minute'] % 60:02d} {d}"
    if a["kind"] == "once":
        return f"once {datetime.fromtimestamp(a['at']):%Y-%m-%d %H:%M} {d}"
    return f"sun {a['event']}{a['offset']:+d} {d}"


def alarm_from(field):
    """The alarm in a POST body (web.c body_alarm), or None."""
    d = {"up": 1, "down": -1}.get(field("d"), 0)
    k = field("k") or "recurring"
    try:
        if k == "recurring":
            m = int(field("m"))
            return {"kind": k, "dir": d, "minute": m} if d and 0 <= m < 1440 else None
        if k == "once":
            if field("at"):
                at = int(field("at"))
            else:
                m = int(field("m"))
                at = int((datetime.strptime(field("date"), "%Y-%m-%d") + timedelta(minutes=m)).timestamp())
            return {"kind": k, "dir": d, "at": at} if d and at % 60 == 0 else None
        if k == "sun":
            o = int(field("o") or 0)
            return {"kind": k, "dir": d, "event": field("e"), "offset": o} \
                if d and field("e") in SUN_EVENTS and -180 <= o <= 180 else None
    except ValueError:
        return None
    return None


def check_alarms():
    global last_minute
    while True:
        now = datetime.now().replace(second=0, microsecond=0)
        with lock:
            if now != last_minute:
                last_minute = now
                once = recurring = 0
                sun = {}
                for a in list(alarms):
                    if a["kind"] == "once" and a["at"] <= now.timestamp():
                        if a["at"] == now.timestamp():
                            once = a["dir"]
                        else:
                            log(f"W alarms: Missed {describe(a)} (the board was off or didn't know the time): removed")
                        alarms.remove(a)
                    elif a["kind"] == "recurring" and a["minute"] == now.hour * 60 + now.minute:
                        recurring = a["dir"]
                    elif a["kind"] == "sun":
                        for days in (-1, 0, 1):
                            t = sun_time(now.date() + timedelta(days=days), a["event"])
                            if t is not None and (t + timedelta(minutes=a["offset"])).replace(second=0) == now:
                                sun[a["event"]] = a["dir"]
                which, d = ("one-time", once) if once else ("recurring", recurring) if recurring else (None, 0)
                for e in SUN_EVENTS:
                    if not d and e in sun:
                        which, d = e, sun[e]
                if d:
                    log(f"I alarms: Alarm ({which}): {'up' if d > 0 else 'down'}")
                    command("up" if d > 0 else "down", "alarm")
        time.sleep(1)


def alarm_json(a, now):
    j = {"kind": a["kind"], "dir": "up" if a["dir"] > 0 else "down"}
    j.update({k: a[k] for k in ("minute", "at", "event", "offset") if k in a})
    t = next_time(a, now)
    j["next"] = int(t.timestamp()) if t else None
    j["when"] = t.strftime("%Y-%m-%d %H:%M") if t else None
    return j


def sun_json(now):
    if location is None:
        return None
    out = {}
    for e in SUN_EVENTS:
        t = sun_time(now.date(), e)
        out[e] = t.hour * 60 + t.minute if t else None
    return out


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
            self.send(200, (WEB_DIR / name).read_bytes(), content_type)   # name may be an absolute Path
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
                    "jog": False,
                    "pos": pos,
                    "stop_top": stops["top"],
                    "stop_bottom": stops["bottom"],
                    "irms": motor["irms"], "mode": motor["mode"],
                    "speed_up": speeds["up"], "speed_down": speeds["down"],
                    "driver": {"current": round(motor["irms"] / 1.7 * 30), "open_a": motor["irms"] > 1.2,
                               "open_b": False, "fault": False},
                    "today": now.strftime("%Y-%m-%d"),
                    "location": {"lat": location[0], "lon": location[1]} if location else None,
                    "sun": sun_json(now),
                    "max_alarms": MAX_ALARMS,
                    "alarms": [alarm_json(a, now) for a in alarms],
                    "stats": {"uptime_s": int(time.monotonic() - STARTED), "heap": 212 * 1024,
                              "heap_min": 190 * 1024, "heap_largest": 110 * 1024, "rssi": -58,
                              "updating": False, "reset": "power on", "version": "mock, " + now.strftime("%b %d %Y")},
                    "log_next": log_next,
                    "log": log_lines[max(0, len(log_lines) - (log_next - since)):],
                }
            self.send(200, json.dumps(status).encode(), "application/json")
        else:
            self.send(404)

    def do_POST(self):
        global STARTED
        if self.path == "/api/ota":
            size = int(self.headers.get("Content-Length", 0))
            data = self.rfile.read(size)
            with lock:
                if remaining_ms():
                    self.send(409, b"The motor is moving: try again when it stops", "text/plain")
                elif not data or data[0] != 0xE9:
                    self.send(400, b"Not a firmware image: send build/carrier_fw.bin", "text/plain")
                else:
                    log(f"I ota: Received {size} bytes (mock): restarting")
                    STARTED = time.monotonic()
                    self.send(200, b"Update written: restarting", "text/plain")
            return
        body = parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode())
        field = lambda name: body.get(name, [""])[0]
        d = {"up": 1, "down": -1}.get(field("d"), 0)
        ok = True
        with lock:
            if self.path == "/api/move" and d:
                command(field("d"), "web page")
            elif self.path == "/api/cancel":
                command("cancel", "web page")
            elif self.path == "/api/jog":
                global pos
                n = int(field("n") or 0)
                ok = d != 0 and 1 <= n <= 48 and not remaining_ms()
                if ok:
                    pos += d * n
                    log(f"I travel: Jog {field('d')} {n}: position {pos}")
            elif self.path == "/api/motor/settings":
                if field("i"):
                    motor["irms"] = max(0.2, min(1.7, float(field("i"))))
                if field("m") in ("spread", "stealth"):
                    motor["mode"] = field("m")
                log(f"I motor: Settings: {motor['irms']:.2f} A, {motor['mode']}")
            elif self.path == "/api/speed":
                v = float(field("v") or 0)
                ok = d != 0 and 0.1 <= v <= 8.0
                if ok:
                    speeds[field("d")] = round(v, 1)
                    log(f"I travel: {field('d').capitalize()} speed set to {v:.1f} rev/s")
            elif self.path in ("/api/stops/set", "/api/stops/clear"):
                w = field("w")
                ok = w in stops
                if ok and self.path.endswith("clear"):
                    stops[w] = None
                    log(f"I travel: {w.capitalize()} stop cleared")
                elif ok:
                    other = stops["bottom" if w == "top" else "top"]
                    if other is not None and (pos <= other if w == "top" else pos >= other):
                        log(f"W travel: Not saved: the {w} stop must be {'above' if w == 'top' else 'below'} the other stop ({other})")
                    else:
                        stops[w] = pos
                        log(f"I travel: {w.capitalize()} stop set at {pos}")
            elif self.path == "/api/alarms/add":
                a = alarm_from(field)
                if a and a["kind"] == "once" and not time.time() < a["at"] <= time.time() + 86400:
                    a = None
                same = next((b for b in alarms if a and same_slot(a, b)), None)
                ok = a is not None and (same is not None or len(alarms) < MAX_ALARMS)
                if ok:
                    if same:
                        alarms[alarms.index(same)] = a
                    else:
                        alarms.append(a)
                    log(f"I alarms: {'Replaced with' if same else 'Added'} {describe(a)}")
            elif self.path == "/api/alarms/remove":
                a = alarm_from(field)
                same = next((b for b in alarms if a and same_slot(a, b) and b["dir"] == a["dir"] and
                             (a["kind"] != "sun" or b["offset"] == a["offset"])), None)
                if same:
                    alarms.remove(same)
                    log(f"I alarms: Removed {describe(same)}")
            elif self.path == "/api/location":
                global location
                if field("clear"):
                    location = None
                    log("I sun: Location cleared")
                else:
                    try:
                        lat, lon = round(float(field("lat")), 1), round(float(field("lon")), 1)
                        ok = -90 <= lat <= 90 and -180 <= lon <= 180
                    except ValueError:
                        ok = False
                    if ok:
                        location = (lat, lon)
                        log(f"I sun: Location set to {lat:.1f}, {lon:.1f}")
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
