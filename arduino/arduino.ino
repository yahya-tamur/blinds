#include <AccelStepper.h>
#include "poll.h"
#include "clock.h"
#include "alarms.h"
#include "wifi.h"
#include "motor.h"
#include "log.h"
#include "memstats.h"


// Which dirPin level counts as 'up'. Swap to LOW/HIGH to reverse.
const bool UP_DIR = LOW;

const unsigned long RUN_MS = 60000;  // time at cruising speed for a full run

// Planned stops partway through a run, so a stalled motor can catch up (see motor.h)
const int RUN_STOPS = 5;

// What the web page counts down from: RUN_MS, plus about 0.5 s lost to each
// slow-down and speed-up pair, the planned stops' rests and the back-off.
const unsigned long RUN_EXPECTED_MS = RUN_MS + (RUN_STOPS + 1) * 500 + RUN_STOPS * PLANNED_STOP_MS + 1500;

// A run started by the remote, the web page or an alarm is in progress
// (until the motor is at rest again, including its back-off)
bool timed_run = false;

const unsigned long MEMORY_REPORT_MS = 5000;
unsigned long last_memory_report = 0;

bool to_dir(int p) {
  return p > 0 ? UP_DIR : !UP_DIR;
}

// A run speeds up, travels with RUN_STOPS planned stops, slows to a stop, then
// turns a quarter turn back
void start_run(int p) {
  configure_driver();
  motor_move(to_dir(p), RUN_MS * STEPS_PER_SECOND / 1000, true, RUN_STOPS);
  set_motor_state(p, RUN_EXPECTED_MS);
  timed_run = true;
}

// Slows to a stop and turns a quarter turn back, as at the end of a run
void cancel_run(const char* message) {
  motor_stop(true);
  set_motor_state(0, 0);
  timed_run = false;
  Log.println(message);
}

void setup() {
  // The program runs on its own, larger stack (see memstats.h)
  run_on_program_stack(program);
}

// Started by setup() on the program's stack: the real setup, then loop()
// forever, as the board's own main() would have done.
void program() {
  Serial.begin(9600);

  initialize_motor();
  initialize_clock();
  initialize_alarms();
  initialize_all();  // connects to WiFi and syncs the RTC

  for (;;) {
    loop();
  }
}

int p = 0;

void loop() {
  if (millis() - last_memory_report >= MEMORY_REPORT_MS) {
    last_memory_report = millis();
    log_memory_stats();
  }

  if (timed_run && !motor_running()) {
    timed_run = false;
    set_motor_state(0, 0);
  }

  int a = check_alarms();
  if (a != 0 && !timed_run) {
    Log.println(a == ALARM_UP ? "Alarm: up" : "Alarm: down");
    start_run(a);
  }

  int source;
  p = poll(&source);

  if (p == CMD_CANCEL) {
    if (timed_run) {
      cancel_run(source == SOURCE_SWITCH ? "Cancelled by switch"
                 : source == SOURCE_REMOTE ? "Cancelled by remote" : "Cancelled from web page");
    }
  } else if (p != 0) {
    // Up/down from the switch, remote or web page start a run; they're ignored while one is going
    if (!timed_run) {
      start_run(p);
    }
  } else {
    delay(100);
  }
}
