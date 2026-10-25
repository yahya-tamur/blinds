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

const unsigned long RUN_MS = 40000;

// How far the motor goes per loop while a switch button is held (0.1 s worth).
// It's topped up every loop, so the motor moves steadily until the button is let go.
const unsigned long JOG_STEPS = STEPS_PER_SECOND / 10;

// A 40 s run started by the remote, the web page or an alarm is in progress
bool timed_run = false;

// The switch press that cancelled a run shouldn't also move the motor
bool wait_for_switch_release = false;

const unsigned long MEMORY_REPORT_MS = 5000;
unsigned long last_memory_report = 0;

bool to_dir(int p) {
  return p > 0 ? UP_DIR : !UP_DIR;
}

void start_run(int p) {
  motor_move(to_dir(p), RUN_MS * STEPS_PER_SECOND / 1000);
  set_motor_state(p, RUN_MS);
  timed_run = true;
}

void cancel_run(const char* message) {
  motor_stop();
  set_motor_state(0, 0);
  timed_run = false;
  Log.println(message);
}

void setup() {
  // put your setup code here, to run once:

  initialize_memory_stats();
  Serial.begin(9600);

  initialize_motor();
  initialize_clock();
  initialize_alarms();
  initialize_all();  // connects to WiFi and syncs the RTC
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

  bool switch_held = source == SOURCE_SWITCH && p != 0;
  if (wait_for_switch_release && !switch_held) {
    wait_for_switch_release = false;
    delay(50);  // let the button settle
  }

  if (switch_held) {
    if (timed_run) {
      cancel_run("Cancelled by switch");
      wait_for_switch_release = true;
    } else if (!wait_for_switch_release) {
      // The switch moves the motor only while it is held
      motor_move(to_dir(p), JOG_STEPS);
    }
  } else if (p == CMD_CANCEL) {
    if (timed_run) {
      cancel_run(source == SOURCE_REMOTE ? "Cancelled by remote" : "Cancelled from web page");
    }
  } else if (p != 0) {
    // Up/down from the remote or web page start a run; they're ignored while one is going
    if (!timed_run) {
      start_run(p);
    }
  } else {
    delay(100);
  }
}
