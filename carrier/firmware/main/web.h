// The web interface: static pages from web/ (embedded in the firmware) and a small JSON API.
//
//   GET  /api/status?since=N   time, motor state, alarms, and log lines from number N on
//   POST /api/move             d=up|down
//   POST /api/cancel
//   POST /api/alarms/add       m=<minutes since midnight>&d=up|down
//   POST /api/alarms/remove    m=...&d=...
#pragma once

void web_start(void);
