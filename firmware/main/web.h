// The web interface: static pages from web/ (embedded in the firmware) and a small JSON API.
//
//   GET  /api/status?since=N   time, motor state, alarms, health stats (memory, Wi-Fi RSSI, uptime,
//                              last reset, version), and log lines from number N on
//   POST /api/move             d=up|down
//   POST /api/cancel
//   POST /api/alarms/add       d=up|down&k=<kind> and its fields (alarms.h); adding one in an existing slot replaces it:
//                                k=recurring&m=<minutes since midnight>             (k may be left out)
//                                k=once&date=YYYY-MM-DD&m=<minutes> (local), or k=once&at=<Unix time, whole minute>; within 24 h
//                                k=sun&e=dawn|sunrise|sunset|dusk&o=<offset minutes, -180..180>
//   POST /api/alarms/remove    the same fields as in the status list (all must match)
//   POST /api/alarms/skip      the same, plus skip=1|0: a recurring or sun alarm skips its next time once, or not
//                              (status: skip = the skipped time "YYYY-MM-DD HH:MM" local and skip_at = the same as
//                              Unix time, or both null; next/when are
//                              then the time after it)
//   POST /api/location         lat=..&lon=.. (degrees, north and east positive, one decimal kept), or clear=1
//   (status also has today (local date), location, sun (today's dawn/sunrise/sunset/dusk, local minutes)
//    and for each alarm next (Unix time) and when ("YYYY-MM-DD HH:MM" local), null if not known)
//   POST /api/jog              d=up|down&n=<Hall counts, 30 degrees of the sprocket each>
//   POST /api/motor/settings   i=<run current A>&m=spread|stealth (either optional): saved (status: irms, mode)
//   POST /api/speed            d=up|down&v=<rev/s, 0.1..8>: run speed, saved (status: speed_up, speed_down)
//   POST /api/stops/set        w=top|bottom: the current position becomes that stop
//   POST /api/stops/clear      w=top|bottom
//   (status also has pos, stop_top, stop_bottom (null if not set) and jog)
//   POST /api/ends/detect      up into the top end stop, then down into the bottom one, at ENDSTOP_SPEED; saved
//   POST /api/ends/home        down into the bottom end stop: the position is known again (400 before detecting)
//   (status also has ends: {top, bottom} or null, pos_known, task ("detect_up", "detect_down", "home" or "")
//    and task_result: how the last one ended, or "")
//   POST /api/remote/learn     on=1|0: learn mode (the next remote press is taken, not acted on; 15 s, renewed by
//                              asking again); answers {"id":n}, the id its capture will carry
//   POST /api/remote/new       a=<address>: a remote named "Remote N" (codes 0..255, decimal or 0x..)
//   POST /api/remote/rename    a=<address>&name=<name, up to 23 bytes>
//   POST /api/remote/delete    a=<address>: the remote and all its buttons
//   POST /api/remote/add       a=<address>&c=<command>&do=up|down|stop: a button (adds its remote if new), saved
//   POST /api/remote/remove    a=<address>&c=<command>
//   (status also has remote: {max, max_remotes, remotes: [{addr, name}], buttons: [{addr, cmd, action}], learning,
//    capture: {id, addr, cmd} or null})
//   POST /api/ota              body = build/carrier_fw.bin: firmware update, then restart (ota.c)
//   GET  /update               the update page
#pragma once
#include "esp_http_server.h"

void web_start(void);
httpd_handle_t web_server(void);   // for extra handlers (the Wi-Fi bring-up test adds one)
