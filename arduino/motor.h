#ifndef MOTOR_H
#define MOTOR_H

// Step pulses come from a hardware timer interrupt, so the motor keeps an even
// pace no matter what loop() is doing (e.g. answering web requests).
#define STEPS_PER_SECOND 2500

void initialize_motor();

// Moves `steps` steps with dirPin at `dir`, replacing any move in progress.
void motor_move(bool dir, unsigned long steps);

void motor_stop();

bool motor_running();

#endif
