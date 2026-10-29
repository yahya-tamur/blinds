// Settings for the blinds application (pins are in board.h)
#pragma once

// Local time zone, as a POSIX TZ rule: US Pacific, DST from the second Sunday in March
// to the first Sunday in November. The DS3231 keeps UTC.
#define APP_TZ            "PST8PDT,M3.2.0,M11.1.0"
#define APP_HOSTNAME      "blinds"

// Which switch contact means "up". The switch is a 3-position toggle: SW1 / off / SW2.
#define SWITCH_UP_PIN     PIN_SW1
#define SWITCH_DOWN_PIN   PIN_SW2
#define SWITCH_SETTLE_MS  30      // a position must read the same this long to count

// IR remote (NEC). IR_ADDRESS -1 accepts any remote; set it to the remote's address
// (shown by test_ir) so other remotes in the house are ignored.
#define IR_ADDRESS        -1
#define IR_CMD_UP         0x07
#define IR_CMD_DOWN       0x15
#define IR_CMD_CANCEL     0x09

#define MAX_ALARMS        20

// Length of a run while the motor is simulated (see motor_sim.c)
#define SIM_RUN_MS        65500

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
#define MOTOR_UP_DIR      0       // DIR pin level for "up" (the Arduino's UP_DIR = LOW); swap to reverse
#define MOTOR_SPREADCYCLE 1       // 1 = SpreadCycle like the Arduino (more torque), 0 = StealthChop (quiet)
