#include "log.h"
#include "motor.h"

#include <Arduino.h>
#include "FspTimer.h"

const int stepPin = 8;
const int dirPin = 9;
const int enablePin = 7;

const int ms1 = 11;
const int ms2 = 12;

// Waits between ticks when there's no step to time (microseconds). All are kept
// under 20 ms so they fit even if the timer we get is a 16-bit one.
const float IDLE_US = 1000;      // checking for a new move while stopped
const float ENABLE_US = 1000;    // letting the driver power up before the first step
const float REVERSE_US = 20000;  // pause at rest before backing off
const int PLANNED_STOP_TICKS = PLANNED_STOP_MS / 20;  // a planned stop, 20 ms at a time

static FspTimer step_timer;
static float timer_hz;     // timer counts per second
static float min_speed;    // the speed of the first and last step of a move

enum Phase : uint8_t { IDLE, MOVING, STOPPING, BACKING_OFF };

// Requests from loop(), picked up by the interrupt. Each request's fields are
// written before its flag is set.
static volatile bool move_requested = false;
static volatile bool req_dir;
static volatile unsigned long req_steps;
static volatile bool req_backoff;
static volatile int req_stops;
static volatile bool stop_requested = false;
static volatile bool stop_backoff;

// The interrupt's own state (phase is also read by motor_running())
static volatile Phase phase = IDLE;
static bool dir;
static unsigned long steps_left = 0;     // until the end of the move, or the next planned stop
static unsigned long later_steps = 0;    // after the next planned stop, if any
static unsigned long segment_steps = 0;  // between planned stops
static int resting = 0;                  // ticks spent so far at a planned stop
static bool backoff_after = false;
static float speed = 0;          // steps per second; 0 at rest
static uint32_t half_step = 0;   // timer counts for half of the current step
static bool step_high = false;
static bool enabled = false;

// Sets when the next tick comes, `us` microseconds from now.
static void wait_us(float us) {
  step_timer.set_period((uint32_t)(timer_hz * us / 1e6f));
}

// Cuts the move short to the distance it needs to slow to a stop.
static void brake() {
  unsigned long needed = (unsigned long)(speed * speed / (2.0f * ACCEL));
  if (steps_left > needed) {
    steps_left = needed;
  }
  later_steps = 0;
  resting = 0;
}

// Two ticks per step: one raises the step pin, the next lowers it. On the
// rising tick it also takes requests from loop() and picks this step's speed,
// which sets how long both ticks of the step last.
static void on_tick(timer_callback_args_t* args) {
  if (step_high) {
    digitalWrite(stepPin, LOW);
    step_high = false;
    step_timer.set_period(half_step);
    return;
  }

  if (stop_requested) {
    stop_requested = false;
    if (phase == MOVING) {
      brake();
      backoff_after = stop_backoff;
      phase = STOPPING;
    }
  }

  if (move_requested) {
    if (phase == IDLE || req_dir == dir) {
      dir = req_dir;
      digitalWrite(dirPin, dir);
      segment_steps = req_steps / (req_stops + 1);
      steps_left = segment_steps;
      later_steps = req_steps - segment_steps;
      resting = 0;
      backoff_after = req_backoff;
      phase = MOVING;
      move_requested = false;
    } else if (phase != STOPPING) {
      // Going the other way: stop first, and take the request once at rest
      brake();
      backoff_after = false;
      phase = STOPPING;
    }
  }

  if (steps_left == 0 && later_steps > 0) {
    // A planned stop partway through the move: rest at a standstill, then carry on
    speed = 0;
    if (resting < PLANNED_STOP_TICKS) {
      resting++;
      wait_us(20000);
      return;
    }
    resting = 0;
    steps_left = min(segment_steps, later_steps);
    later_steps -= steps_left;
  }

  if (steps_left == 0) {
    speed = 0;
    if (backoff_after) {
      backoff_after = false;
      dir = !dir;
      digitalWrite(dirPin, dir);
      steps_left = BACKOFF_STEPS;
      phase = BACKING_OFF;
      wait_us(REVERSE_US);
      return;
    }
    phase = IDLE;
    if (enabled) {
      digitalWrite(enablePin, HIGH);
      enabled = false;
    }
    wait_us(IDLE_US);
    return;
  }

  if (!enabled) {
    digitalWrite(enablePin, LOW);
    enabled = true;
    wait_us(ENABLE_US);
    return;
  }

  // Slow down once the steps left are just enough to stop in; otherwise speed
  // up towards cruising speed. Either way the speed changes by the same amount
  // of acceleration over this one step.
  float stopping_steps = speed * speed / (2.0f * ACCEL);
  if (steps_left <= stopping_steps) {
    speed = sqrtf(max(speed * speed - 2.0f * ACCEL, min_speed * min_speed));
  } else if (speed < STEPS_PER_SECOND) {
    speed = min((float)STEPS_PER_SECOND, sqrtf(speed * speed + 2.0f * ACCEL));
  }

  digitalWrite(stepPin, HIGH);
  step_high = true;
  steps_left--;
  half_step = (uint32_t)(timer_hz / (2.0f * speed));
  step_timer.set_period(half_step);
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
  // Starting at 50 Hz makes the timer pick a clock slow enough for the longest
  // waits; after that each tick sets the length of the next one.
  if (!(step_timer.begin(TIMER_MODE_PERIODIC, type, channel, 50.0f, 0.0f, on_tick)
        // Priority 0, the highest: other interrupts (USB and the WiFi chip's
        // serial line at 12, millis() at 14, other timers at 8) can't delay a
        // step, though a step can briefly delay them
        && step_timer.setup_overflow_irq(0)
        && step_timer.open()
        && step_timer.set_period_buffer(false))) {  // new periods apply right away
    return false;
  }
  timer_hz = step_timer.get_freq_hz();
  return step_timer.start();
}

// The driver's settings, sent over its single-wire UART: Arduino D1 (Serial1 TX)
// to the driver's RX pin. Only sending is wired up -- reading back needs a
// resistor between D1 and that pin, and D0 on it too -- so there's no
// confirmation that a setting arrived.
const uint8_t DRIVER_ADDRESS = 0;  // set by MS1 and MS2, both LOW
const uint8_t REG_GCONF = 0x00;
const uint8_t REG_IHOLD_IRUN = 0x10;

// GCONF bits. I_scale_analog (bit 0) stays 0: the current comes from
// IHOLD_IRUN below, not the VREF potentiometer.
const uint32_t EN_SPREADCYCLE = 1 << 2;    // SpreadCycle instead of StealthChop
const uint32_t PDN_DISABLE = 1 << 6;       // the PDN_UART pin is used for UART only
const uint32_t MULTISTEP_FILT = 1 << 8;    // the driver's default step filtering
// (mstep_reg_select, bit 7, stays 0: microstepping still comes from MS1 and MS2)

static void write_driver_register(uint8_t reg, uint32_t value) {
  uint8_t datagram[8] = {
      0x05, DRIVER_ADDRESS, (uint8_t)(reg | 0x80),
      (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value,
      0};
  // CRC8 (polynomial 0x07) over the first 7 bytes, each byte least significant bit first
  uint8_t crc = 0;
  for (int i = 0; i < 7; i++) {
    uint8_t byte = datagram[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = ((crc >> 7) ^ (byte & 1)) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
      byte >>= 1;
    }
  }
  datagram[7] = crc;
  Serial1.write(datagram, sizeof(datagram));
  Serial1.flush();
}

// The current scale (0-31) for an RMS current. On this board (110 mOhm sense
// resistors, plus the chip's own 20 mOhm) full scale is
// 325 mV / 130 mOhm / sqrt(2) = 1768 mA RMS, in 32 equal steps.
static uint32_t current_scale(unsigned long ma) {
  long cs = lround(ma * 32.0 / 1768.0) - 1;
  return (uint32_t)constrain(cs, 0, 31);
}

void configure_driver() {
  write_driver_register(REG_GCONF, EN_SPREADCYCLE | PDN_DISABLE | MULTISTEP_FILT);
  // Run current, and the same while holding still with the driver on (at the
  // planned stops); IHOLDDELAY 1 is the driver's shortest switch between them
  uint32_t cs = current_scale(RUN_CURRENT_MA);
  write_driver_register(REG_IHOLD_IRUN, cs | (cs << 8) | (1UL << 16));
}

void initialize_motor() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);

  pinMode(ms1, OUTPUT);
  pinMode(ms2, OUTPUT);

  // TMC2209 standalone: MS1=LOW, MS2=LOW -> 1/8 microstepping
  // (MS1=HIGH, MS2=LOW is 1/32; both HIGH is 1/16)
  digitalWrite(ms1, LOW);
  digitalWrite(ms2, LOW);

  min_speed = sqrtf(2.0f * ACCEL);

  Serial1.begin(115200);  // the driver works out the baud rate itself
  configure_driver();
  Log.print("Driver: SpreadCycle, current scale ");
  Log.print((unsigned long)current_scale(RUN_CURRENT_MA));
  Log.println("/31, requested over UART");

  if (!start_timer()) {
    Log.println("Couldn't start the step timer.");
  } else {
    // How finely step times can be set: one count of this clock
    Log.print("Step timer clock: ");
    Log.print((unsigned long)timer_hz);
    Log.println(" Hz");
  }
}

void motor_move(bool direction, unsigned long steps, bool backoff, int stops) {
  move_requested = false;
  req_dir = direction;
  req_steps = steps;
  req_backoff = backoff;
  req_stops = stops;
  move_requested = true;
}

void motor_stop(bool backoff) {
  move_requested = false;  // a move not yet started is simply dropped
  stop_backoff = backoff;
  stop_requested = true;
}

bool motor_running() {
  return phase != IDLE || move_requested;
}
