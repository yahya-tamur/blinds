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
  - remote buttons are kept in memory; there is no receiver, so POST /api/mock/press a=<address>&c=<command>
    (mock only) stands for a press, e.g. curl -d a=0x02 -d c=0x0c http://127.0.0.1:8766/api/mock/press
  - the blinds have physical end stops at positions MOCK_END_TOP / MOCK_END_BOTTOM; detecting and re-homing
    move at MOCK_END_RATE counts/s (faster than the board, to keep tests short). POST /api/mock/powercut (mock
    only) stands for a restart during a move: the position becomes unknown and the count drifts
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

MOCK_END_TOP, MOCK_END_BOTTOM = 15, -90   # where the blinds' physical stops are, in the true position
MOCK_END_RATE = 30                          # counts per second while detecting or re-homing
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
drift = 0             # counted position minus the true one (a power cut changes it)
ends = None           # {"top", "bottom"} once detected
pos_known = True
task = None           # {"name": "detect_up"|"detect_down"|"home", "start": monotonic, "from": counted pos, "then": dir}
task_result = ""
found_top = None
MAX_REMOTE_BUTTONS = 24
MAX_REMOTES = 8
remotes = []          # {"addr", "name"}, like remote.c
LEARN_S = 15
remote_buttons = []   # {"addr", "cmd", "action": "up"|"down"|"stop"}, like remote.c
learn = {"id": 0, "until": None, "capture": None}   # until: time.monotonic() while learning


def log(line):
    global log_next
    print(line, flush=True)
    log_lines.append(line)
    del log_lines[:-LOG_LINES]
    log_next += 1


def task_target(t):
    """Counted position the task's move ends at (the physical stop), and how long it takes."""
    true_end = MOCK_END_TOP if t["name"] == "detect_up" else MOCK_END_BOTTOM
    end = true_end + drift
    return end, abs(end - t["from"]) / MOCK_END_RATE


def advance_task():
    """Moves the task on: travel.c task_move_ended."""
    global task, pos, ends, pos_known, drift, task_result, found_top
    while task is not None:
        end, secs = task_target(task)
        elapsed = time.monotonic() - task["start"]
        if elapsed < secs:
            d = 1 if end > task["from"] else -1
            pos = task["from"] + d * int(elapsed * MOCK_END_RATE)
            return
        pos = end
        log(f"W travel: Stall: no Hall edge for 2.5 counts' worth of steps (position {pos}). Stopping.")
        if task["name"] == "detect_up":
            found_top = pos
            log(f"I travel: Top end stop at {pos}; now down into the bottom one")
            task = {"name": "detect_down", "start": time.monotonic(), "from": pos, "then": 0}
        elif task["name"] == "detect_down":
            d = -pos                     # renumber so the bottom end stop is 0 (travel.c shift_positions)
            drift += d
            ends = {"top": found_top + d, "bottom": 0}
            pos = 0
            pos_known = True
            log(f"I travel: Position renumbered by {d:+d} so the bottom end stop is 0 (the stops keep their numbers)")
            log(f"I travel: End stops saved: top {ends['top']}, bottom 0")
            task_result = f"Found both: {ends['top']} counts apart."
            task = None
        else:
            then = task["then"]
            drift += ends["bottom"] - pos
            pos = ends["bottom"]
            pos_known = True
            log(f"I travel: Re-homed: at the bottom end stop, position {pos}")
            task_result = "Re-homed on the bottom end stop."
            task = None
            if then:
                command("up" if then > 0 else "down", "re-homing")


def start_task(name, then=0):
    global task, task_result
    task_result = ""
    task = {"name": name, "start": time.monotonic(), "from": pos, "then": then}
    log(f"I travel: {'Detecting end stops: up into the top one' if name == 'detect_up' else 'Re-homing: down into the bottom end stop'} at 2.0 rev/s")


def remaining_ms():
    global run
    if run is not None and (time.monotonic() - run[1]) * 1000 >= RUN_MS:
        run = None
    return 0 if run is None else max(0, RUN_MS - int((time.monotonic() - run[1]) * 1000))


def command(cmd, source):
    """The control task's rules: up/down start a run unless one is going, cancel ends it."""
    global run, task, task_result
    if cmd == "cancel":
        if task is not None:
            log("I control: Cancelled by " + source)
            task_result = "Stopped before reaching the end stop" + (": nothing saved." if task["name"] != "home" else ": position still unknown.")
            task = None
        elif remaining_ms():
            run = None
            log(f"I control: Cancelled by {source}")
    elif not remaining_ms() and task is None:
        global pos
        log(f"I control: {cmd.capitalize()} from {source}")
        if not pos_known and ends is not None and source != "re-homing":
            start_task("home", 1 if cmd == "up" else -1)
            return
        stop = stops["top" if cmd == "up" else "bottom"]
        if stop is None:
            log(f"W travel: No {'top' if cmd == 'up' else 'bottom'} stop set: not running (set it under Adjust stops)")
            return
        if (stop - pos) * (1 if cmd == "up" else -1) <= 0:
            log(f"I travel: Already at or past the {'top' if cmd == 'up' else 'bottom'} stop")
            return
        pos = stop          # the mock "arrives" at once; the page still shows the run
        run = (1 if cmd == "up" else -1, time.monotonic())


def code(v):
    """A code from a POST body (web.c body_code): 0..255, decimal or 0x hex; None if not."""
    try:
        n = int(v, 0)
    except ValueError:
        return None
    return n if 0 <= n <= 255 else None


def new_remote(addr):
    """remote.c new_remote_locked: the remote (added as "Remote N" if new), or None if full."""
    r = next((r for r in remotes if r["addr"] == addr), None)
    if r is None and len(remotes) < MAX_REMOTES:
        names = {r["name"] for r in remotes}
        k = 1
        while f"Remote {k}" in names:
            k += 1
        r = {"addr": addr, "name": f"Remote {k}"}
        remotes.append(r)
        log(f"I remote: Added {r['name']} (address 0x{addr:02X})")
    return r


def learning():
    if learn["until"] is not None and time.monotonic() >= learn["until"]:
        learn["until"] = None
    return learn["until"] is not None


def remote_press(addr, cmd):
    """What remote.c does with a decoded frame."""
    if learning():
        learn["until"] = None
        learn["capture"] = {"id": learn["id"], "addr": addr, "cmd": cmd}
        log(f"I remote: Address 0x{addr:02X} command 0x{cmd:02X} (learned)")
        return
    b = next((b for b in remote_buttons if b["addr"] == addr and b["cmd"] == cmd), None)
    log(f"I remote: Address 0x{addr:02X} command 0x{cmd:02X}{'' if b else ' (not set up)'}")
    if b:
        command("cancel" if b["action"] == "stop" else b["action"], "remote")


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
                    else:
                        due = a["kind"] == "recurring" and a["minute"] == now.hour * 60 + now.minute
                        if a["kind"] == "sun":
                            for days in (-1, 0, 1):
                                t = sun_time(now.date() + timedelta(days=days), a["event"])
                                if t is not None and (t + timedelta(minutes=a["offset"])).replace(second=0) == now:
                                    due = True
                        skip = a.get("skip_at")
                        if due and skip and skip.replace(second=0) == now:
                            log(f"I alarms: Skipped {describe(a)} this once")
                            a["skip_at"] = None
                        elif due and a["kind"] == "recurring":
                            recurring = a["dir"]
                        elif due:
                            sun[a["event"]] = a["dir"]
                        elif skip and skip.replace(second=0) < now:
                            a["skip_at"] = None
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
    skip = a.get("skip_at")
    skip = skip if skip and skip > now else None
    j["skip"] = skip.strftime("%Y-%m-%d %H:%M") if skip else None
    j["skip_at"] = int(skip.timestamp()) if skip else None
    t = next_time(a, now)
    if t and skip and t.replace(second=0) == skip.replace(second=0):   # alarms_next_run: the time after the skipped one
        t = next_time(a, t)
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
                advance_task()
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
                    "ends": ends, "pos_known": pos_known, "task": task["name"] if task else "",
                    "task_result": task_result,
                    "irms": motor["irms"], "mode": motor["mode"],
                    "speed_up": speeds["up"], "speed_down": speeds["down"],
                    "driver": {"current": round(motor["irms"] / 1.7 * 30), "open_a": motor["irms"] > 1.2,
                               "open_b": False, "fault": False},
                    "today": now.strftime("%Y-%m-%d"),
                    "location": {"lat": location[0], "lon": location[1]} if location else None,
                    "sun": sun_json(now),
                    "max_alarms": MAX_ALARMS,
                    "alarms": [alarm_json(a, now) for a in alarms],
                    "remote": {"max": MAX_REMOTE_BUTTONS, "max_remotes": MAX_REMOTES, "remotes": remotes,
                               "buttons": remote_buttons, "learning": learning(),
                               "capture": learn["capture"]},
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
            advance_task()
            busy = task is not None or remaining_ms() > 0
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
            elif self.path == "/api/alarms/skip":
                a = alarm_from(field)
                same = next((b for b in alarms if a and same_slot(a, b) and b["dir"] == a["dir"] and
                             (a["kind"] != "sun" or b["offset"] == a["offset"])), None)
                ok = same is not None and same["kind"] != "once" and field("skip") in ("0", "1")
                if ok and field("skip") == "1":
                    t = next_time(same, datetime.now())
                    ok = t is not None
                    if ok:
                        same["skip_at"] = t
                        log(f"I alarms: {describe(same)} skips {t:%Y-%m-%d %H:%M} once")
                elif ok:
                    same["skip_at"] = None
                    log(f"I alarms: {describe(same)} no longer skips")
            elif self.path == "/api/alarms/remove":
                a = alarm_from(field)
                same = next((b for b in alarms if a and same_slot(a, b) and b["dir"] == a["dir"] and
                             (a["kind"] != "sun" or b["offset"] == a["offset"])), None)
                if same:
                    alarms.remove(same)
                    log(f"I alarms: Removed {describe(same)}")
            elif self.path == "/api/remote/learn":
                ok = field("on") in ("0", "1")
                if ok:
                    if field("on") == "1":
                        if not learning():
                            learn["id"] += 1
                        learn["until"] = time.monotonic() + LEARN_S
                    else:
                        learn["until"] = None
                    body = json.dumps({"id": learn["id"]}).encode()
            elif self.path == "/api/remote/add":
                a, c, act = code(field("a")), code(field("c")), field("do")
                same = next((b for b in remote_buttons if b["addr"] == a and b["cmd"] == c), None)
                ok = a is not None and c is not None and act in ("up", "down", "stop") and \
                    (same is not None or (len(remote_buttons) < MAX_REMOTE_BUTTONS and new_remote(a) is not None))
                if ok:
                    if same:
                        same["action"] = act
                    else:
                        remote_buttons.append({"addr": a, "cmd": c, "action": act})
                    log(f"I remote: {'Changed' if same else 'Added'} address 0x{a:02X} command 0x{c:02X}: {act}")
            elif self.path == "/api/remote/remove":
                a, c = code(field("a")), code(field("c"))
                ok = a is not None and c is not None
                same = next((b for b in remote_buttons if b["addr"] == a and b["cmd"] == c), None)
                if same:
                    remote_buttons.remove(same)
                    log(f"I remote: Removed address 0x{a:02X} command 0x{c:02X}")
            elif self.path == "/api/remote/new":
                a = code(field("a"))
                ok = a is not None and new_remote(a) is not None
            elif self.path == "/api/remote/rename":
                a, name = code(field("a")), field("name").strip()
                while len(name.encode()) > 23:
                    name = name[:-1]
                name = name.strip()
                r = next((r for r in remotes if r["addr"] == a), None)
                ok = r is not None and name != ""
                if ok:
                    log(f"I remote: Renamed {r['name']} to {name}")
                    r["name"] = name
            elif self.path == "/api/remote/delete":
                a = code(field("a"))
                ok = a is not None
                if ok and any(r["addr"] == a for r in remotes):
                    remotes[:] = [r for r in remotes if r["addr"] != a]
                    remote_buttons[:] = [b for b in remote_buttons if b["addr"] != a]
                    log(f"I remote: Removed the remote at address 0x{a:02X} and its buttons")
            elif self.path == "/api/ends/detect":
                if not busy:
                    start_task("detect_up")
            elif self.path == "/api/ends/home":
                ok = ends is not None
                if ok and not busy:
                    start_task("home")
            elif self.path == "/api/mock/powercut":
                global drift, pos_known
                drift += 7
                pos += 7
                pos_known = False
                log("W travel: The board restarted during a move: position unknown. "
                    + ("The next run re-homes on the bottom end stop first." if ends else "Detect the end stops so it can re-home (web page)."))
            elif self.path == "/api/mock/press":
                a, c = code(field("a")), code(field("c"))
                ok = a is not None and c is not None
                if ok:
                    remote_press(a, c)
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
        if ok and self.path == "/api/remote/learn":
            self.send(200, body, "application/json")
        else:
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
