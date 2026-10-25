#include "log.h"
#include "motor.h"

#include <Arduino.h>
#include "FspTimer.h"

const int stepPin = 8;
const int dirPin = 9;
const int enablePin = 7;

const int ms1 = 1;
const int ms2 = 2;

static FspTimer step_timer;

// Shared with the interrupt. loop() only ever sets steps_left; the interrupt
// does the counting and turns the driver on and off.
static volatile unsigned long steps_left = 0;
static volatile bool step_high = false;
static volatile bool enabled = false;

// Runs at twice the step rate: one tick raises the step pin, the next lowers it.
static void on_tick(timer_callback_args_t* args) {
  if (step_high) {
    digitalWrite(stepPin, LOW);
    step_high = false;
    return;
  }
  if (steps_left == 0) {
    if (enabled) {
      digitalWrite(enablePin, HIGH);
      enabled = false;
    }
    return;
  }
  if (!enabled) {
    digitalWrite(enablePin, LOW);
    enabled = true;
  }
  digitalWrite(stepPin, HIGH);
  step_high = true;
  steps_left--;
}

static bool start_timer() {
  uint8_t type = GPT_TIMER;
  int8_t channel = FspTimer::get_available_timer(type);
  if (channel < 0) {
    channel = FspTimer::get_available_timer(type, true);
  }
  if (channel < 0) {
    return false;
  }
  return step_timer.begin(TIMER_MODE_PERIODIC, type, channel, 2.0f * STEPS_PER_SECOND, 0.0f, on_tick)
      && step_timer.setup_overflow_irq()
      && step_timer.open()
      && step_timer.start();
}

void initialize_motor() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);

  pinMode(ms1, OUTPUT);
  pinMode(ms2, OUTPUT);

  // TMC2209 standalone: MS1=HIGH, MS2=HIGH -> 1/16 microstepping
  digitalWrite(ms1, HIGH);
  digitalWrite(ms2, HIGH);

  if (!start_timer()) {
    Log.println("Couldn't start the step timer.");
  }
}

void motor_move(bool dir, unsigned long steps) {
  digitalWrite(dirPin, dir);
  steps_left = steps;
}

void motor_stop() {
  steps_left = 0;
}

bool motor_running() {
  return steps_left > 0;
}
