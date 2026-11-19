// Settings for the blinds application (pins are in board.h)
#pragma once

// Local time zone, as a POSIX TZ rule: US Pacific, DST from the second Sunday in March
// to the first Sunday in November. The DS3231 keeps UTC.
#define APP_TZ            "PST8PDT,M3.2.0,M11.1.0"
#define APP_HOSTNAME      "blinds"

// Which switch contact means "up". The switch is a momentary (ON)-OFF-(ON) toggle: SW1 / off / SW2.
#define SWITCH_UP_PIN     PIN_SW1
#define SWITCH_DOWN_PIN   PIN_SW2
#define SWITCH_SETTLE_MS  30      // a position must read the same this long to count

// IR remote buttons are set up on the web page (Set up remote) and saved on the board (remote.c).

#define MAX_ALARMS        20

// Motor runs (motor_steps.c), no planned stops yet. These match the Arduino version: 833 steps/s at
// 1/8 microstepping for 60 s of cruising = 31.24 motor revolutions at 0.52 rev/s, speeding up and
// slowing down at 1666 steps/s^2 (full speed in 0.5 s), then (also after a cancel) a pause and a
// quarter turn back.
#define MOTOR_MICROSTEPS  16
#define MOTOR_RUN_REVS    31.24f
#define MOTOR_REV_PER_S   0.5206f
#define MOTOR_ACCEL       1.041f   // rev/s^2
#define MOTOR_BACKOFF_REVS 0.25f
#define MOTOR_BACKOFF_PAUSE_MS 20
#define MOTOR_UP_DIR      0       // DIR pin level for "up" (the Arduino's UP_DIR = LOW); travel.c swaps it at run time
                                  // (saved in NVS "mot_rev") when the Hall count shows the motor was rewired
#define MOTOR_SPREADCYCLE 0       // 0 = StealthChop: quiet, and in the lift tests it kept its torque at speed (SpreadCycle with
                                  // default chopper settings lost it above ~1.3 rev/s); 1 = SpreadCycle
// Hall position (travel.c): the gear turns once per MOTOR_TURNS_PER_SPROCKET motor turns, giving 12 counts.
// HALL_UP_SIGN makes the position increase going up (+1 on v0.2.0 with the motor going down on Down:
// the count falls going down). It follows the sensors' wiring, not the motor's: if the count runs against a
// move, travel.c takes it that the motor was rewired and swaps the motor direction.
#define MOTOR_TURNS_PER_SPROCKET 4
// Run speeds (travel.c): Up and Down are set on the web page and saved; these are the defaults. Tests on the
// blinds (2026-11-13): Up worked to 7 rev/s (stalled at 8 just after lift-off), Down to 8.
#define MOTOR_SPEED_DEFAULT 5.0f    // rev/s, both directions
#define MOTOR_JOG_SPEED     2.0f    // rev/s, for the stop-setting nudges
#define MOTOR_RETRY_SPEED   2.0f    // rev/s: a run to a stop that stalls backs off, then finishes at this speed
#define MOTOR_MAX_SPEED     8.0f
// End stops (travel.c): the blinds' physical stops at both ends, found by running into them until the Hall
// count stops (a stall). Detect endstops (web page) finds both; after a power cut during a move the position
// is unknown, and the next run first lowers into the bottom end stop to find it again.
#define ENDSTOP_SPEED       2.0f    // rev/s, for detecting and re-homing
#define ENDSTOP_MAX_COUNTS  240     // detecting gives up after this far in one direction (20 turns of the sprocket)
#define HOME_EXTRA_COUNTS   24      // re-homing runs at most the end stops' distance apart plus this
#define HALL_UP_SIGN      (1)
