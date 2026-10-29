#ifndef MOTOR_H
#define MOTOR_H

// Step pulses come from a hardware timer interrupt, so the motor keeps an even
// pace no matter what loop() is doing (e.g. answering web requests). Every move
// speeds up from rest, cruises, and slows down again before it stops.

#define STEPS_PER_REV 1600      // 200-step motor at 1/8 microstepping
#define STEPS_PER_SECOND 833    // cruising speed: about 0.52 rev/s
#define ACCEL 1666              // steps/s^2: full speed from rest in 0.5 s

// How far the motor turns back the other way after a run stops (a quarter turn)
#define BACKOFF_STEPS (STEPS_PER_REV / 4)

// Motor current, set over UART instead of the driver's VREF potentiometer. This
// is the RMS current; the peak is 1.41x it. 1414 mA RMS = 2.0 A peak, the
// motor's rated 2.0 A per phase. The driver can do up to about 1770 mA, but it
// runs hot well before that.
#define RUN_CURRENT_MA 1414

// How long the motor rests, still powered, at each planned stop partway
// through a move (see motor_move). Slowing to a standstill and starting again
// lets a stalled motor catch up.
#define PLANNED_STOP_MS 200

void initialize_motor();

// Sends the driver its settings over UART (SpreadCycle mode, RUN_CURRENT_MA). Done at startup,
// and again before each run, since the driver forgets them if its motor power
// is cut while the Arduino stays on.
void configure_driver();

// Moves `steps` steps with dirPin at `direction`, ending at rest. If the motor is
// already going that way it carries on from its current speed with the new
// count; if it's going the other way it slows to a stop first. The move is split
// into `stops` + 1 equal parts, with a planned stop between each. With
// `backoff`, it then turns BACKOFF_STEPS the other way.
void motor_move(bool direction, unsigned long steps, bool backoff, int stops);

// Slows a move to a stop as quickly as ACCEL allows, then backs off if asked.
// Does nothing if the motor is already stopping or backing off.
void motor_stop(bool backoff);

// True until the motor has come to rest, including any back-off.
bool motor_running();

#endif
