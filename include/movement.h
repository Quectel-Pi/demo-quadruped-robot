#ifndef MOVEMENT_H
#define MOVEMENT_H

#include "servo_control.h"

/* A command target, NOT a measurement of the mechanical joint position. */
typedef struct {
	int angles[SERVO_COUNT];
	unsigned duration_ms;
} motion_frame_t;

typedef struct {
	servo_control_t *control;
	const volatile sig_atomic_t *stopping;
	int last_commanded[SERVO_COUNT];
	unsigned known_mask;
} movement_session_t;

/* Does not arm or move anything. A new process starts with NO known targets. */
int movement_session_init(movement_session_t *session, servo_control_t *control,
						  const volatile sig_atomic_t *stopping);
/* Record externally confirmed logical command targets (no PWM write/read).
 * The CLI uses movement_stand() instead; never use guessed mechanical angles. */
int movement_confirm_start(movement_session_t *session, const int confirmed[SERVO_COUNT]);
/* Unknown start: write the first frame directly, then record sent targets.
 * Known start: use 20ms monotonic interpolation. Targets are not feedback;
 * stop/error aborts the sequence and write failures invalidate the start. */
int movement_play(movement_session_t *session, const motion_frame_t *frames, size_t count);
int movement_stand(movement_session_t *session, unsigned duration_ms);
/* Return to the single standard pose: upper CH4..7, then lower CH0..3.
 * Zero duration selects movement.c defaults; unknown targets are written directly. */
int movement_restore_stand(movement_session_t *session,
						   unsigned upper_ms, unsigned lower_ms);
/* Unimplemented motion directions/limits are not calibrated and must not write PWM. */
int movement_rest(movement_session_t *session);
int movement_forward(movement_session_t *session);
/* Graceful stop is checked between complete wave cycles. Use a separate
 * session->stopping flag for immediate aborts; keep it clear for recovery. */
int movement_hello_right_loop(movement_session_t *session,
							  const volatile sig_atomic_t *stop_requested);
int movement_hello_left_loop(movement_session_t *session,
							 const volatile sig_atomic_t *stop_requested);
int movement_backward(movement_session_t *session);
int movement_sit(movement_session_t *session);
/* Web-only graceful hold: sit once, then wait without PWM until recovery. */
int movement_sit_loop(movement_session_t *session,
					  const volatile sig_atomic_t *stop_requested);
/* Graceful stop after the current turn frame, followed by upper-then-lower recovery. */
int movement_turn_right_loop(movement_session_t *session,
							 const volatile sig_atomic_t *stop_requested);
int movement_turn_left_loop(movement_session_t *session,
							const volatile sig_atomic_t *stop_requested);
/* Play the full preparation frame once, then interpolate upper channels only
 * through the two 500 ms frames until graceful stop; restore stand once.
 * stop_requested must be separate from session->stopping, which remains clear
 * so a graceful stop cannot abort PWM writes or the stand recovery. */
int movement_dance_loop(movement_session_t *session,
							const volatile sig_atomic_t *stop_requested);
int movement_dance2_loop(movement_session_t *session,
							 const volatile sig_atomic_t *stop_requested);

#endif