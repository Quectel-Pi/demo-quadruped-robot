#ifndef FACE_H
#define FACE_H

#include "face_bitmaps.h"
#include "oled.h"

typedef enum {
	FACE_NEUTRAL,
	FACE_HAPPY,
	FACE_BLINK,
	FACE_SAD,
	FACE_WORRIED,
	FACE_SURPRISED,
	FACE_SLEEPY,
	FACE_ANGRY,
	FACE_AWE,
	FACE_COUNT
} face_id_t;

typedef enum {
    FACE_PHASE_HOLD,
    FACE_PHASE_ENTER,
    FACE_PHASE_BLINK,
    FACE_PHASE_LOOK,
    FACE_PHASE_MICRO
} face_phase_t;

typedef struct {
	oled_t *oled;                    /* Borrowed: owned by the caller. */
	face_bitmaps_t bitmaps;
	face_id_t current;
	face_phase_t phase;
	unsigned step;
	unsigned pose;
	unsigned blink_step;
	unsigned pending_second;
	unsigned blinking;
	unsigned dirty;
	uint32_t random_state;
	uint64_t frame_at_ms;
	uint64_t blink_at_ms;
	uint64_t look_at_ms;
	uint64_t micro_at_ms;
} face_t;

int face_init(face_t *face, oled_t *oled);
int face_set(face_t *face, face_id_t id);
int face_set_idle(face_t *face);
/* Call often from the main loop. Sends I2C only when the image changes. */
int face_update(face_t *face);
void face_close(face_t *face); /* Does not close the caller-owned OLED. */
const char *face_name(face_id_t id);

#endif