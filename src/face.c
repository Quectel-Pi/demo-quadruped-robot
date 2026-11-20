#include "face.h"

#include <errno.h>
#include <time.h>

enum {
    BLINK_FRAME_MS = 45,
    DOUBLE_BLINK_GAP_MS = 290,
    BLINK_INTERVAL_MS = 1000,
    LOOK_MIN_MS = 9000,
    LOOK_SPAN_MS = 7001,
    LOOK_STEP_MS = 320,
    HAPPY_BOUNCE_MS = 170,
    SAD_SINK_MS = 1250,
    WORRIED_LOOK_MS = 380,
    ANGRY_GLARE_MS = 130,
    SLEEPY_Z_MS = 560,
    AWE_SPARKLE_MS = 380
};

/* Entry poses are rendered by face_bitmap_render; pose 0 is always the
 * verified resting drawing. Durations belong to the pose at the same index. */
typedef struct {
    eyes_image_t image;
    unsigned count;
    unsigned pose[5];
    unsigned ms[5];
    unsigned micro_wait_ms;
} face_timing_t;

static const face_timing_t timing[FACE_COUNT] = {
    [FACE_NEUTRAL]   = {EYES_NEUTRAL,   0, {0},          {0},                    0},
    [FACE_HAPPY]     = {EYES_HAPPY,     5, {1,2,3,4,0},  {85,85,110,150,0},     6000},
    [FACE_BLINK]     = {EYES_NEUTRAL,   0, {0},          {0},                    0},
    [FACE_SAD]       = {EYES_SAD,       4, {1,2,3,0},    {170,180,210,0},       2300},
    [FACE_WORRIED]   = {EYES_WORRIED,   3, {1,2,0},      {145,145,0},           5100},
    [FACE_SURPRISED] = {EYES_SURPRISED, 4, {1,2,3,0},    {80,110,140,0},        0},
    [FACE_SLEEPY]    = {EYES_SLEEPY,    5, {1,2,3,4,0},  {200,230,260,300,0},  SLEEPY_Z_MS},
    [FACE_ANGRY]     = {EYES_ANGRY,     4, {1,2,3,0},    {100,120,140,0},      6800},
    [FACE_AWE]       = {EYES_AWE,       4, {1,2,3,0},    {80,105,135,0},        1400}
};

static int monotonic_ms(uint64_t *ms)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    *ms = (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
    return 0;
}

static uint32_t random_next(face_t *face)
{
    uint32_t x = face->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    face->random_state = x;
    return x;
}

static void schedule_blink(face_t *face, uint64_t now)
{
    face->blink_at_ms = now + BLINK_INTERVAL_MS;
}

static void schedule_neutral(face_t *face, uint64_t now)
{
    schedule_blink(face, now);
    face->look_at_ms = now + LOOK_MIN_MS + random_next(face) % LOOK_SPAN_MS;
}

static uint64_t micro_wait(face_t *face)
{
    unsigned wait = timing[face->current].micro_wait_ms;
    if (face->current == FACE_HAPPY || face->current == FACE_WORRIED ||
        face->current == FACE_ANGRY) wait += random_next(face) % 2401U;
    return wait;
}

static void start_blink(face_t *face, uint64_t now, int already_neutral)
{
    face->phase = FACE_PHASE_BLINK;
    face->blinking = 1;
    face->blink_step = 0;
    face->step = 0;
    face->pose = 0;
    face->frame_at_ms = now + BLINK_FRAME_MS;
    schedule_blink(face, now);
    if (!already_neutral) face->dirty = 1;
}

const char *face_name(face_id_t id)
{
    static const char *const names[FACE_COUNT] = {
        "neutral", "happy", "blink", "sad", "worried",
        "surprised", "sleepy", "angry", "awe"
    };
    return id >= FACE_NEUTRAL && id < FACE_COUNT ? names[id] : NULL;
}

int face_init(face_t *face, oled_t *oled)
{
    if (!face || !oled || oled->fd < 0) { errno = EINVAL; return -1; }
    uint64_t now;
    if (monotonic_ms(&now) < 0) return -1;
    face_bitmaps_generate(&face->bitmaps);
    face->oled = oled;
    face->current = FACE_NEUTRAL;
    face->phase = FACE_PHASE_HOLD;
    face->step = face->pose = face->blink_step = 0;
    face->pending_second = face->blinking = 0;
    face->frame_at_ms = face->micro_at_ms = 0;
    face->random_state = (uint32_t)(now ^ (now >> 32) ^ (uint64_t)clock());
    if (!face->random_state) face->random_state = 1;
    schedule_neutral(face, now);
    face->dirty = 1;
    return 0;
}

int face_set(face_t *face, face_id_t id)
{
    if (!face || !face->oled || !face_name(id)) { errno = EINVAL; return -1; }
    uint64_t now;
    if (monotonic_ms(&now) < 0) return -1;
    face->current = id;
    face->phase = FACE_PHASE_HOLD;
    face->step = face->pose = face->blink_step = 0;
    face->blinking = face->pending_second = 0;
    face->frame_at_ms = face->micro_at_ms = 0;
    schedule_neutral(face, now);
    if (id == FACE_BLINK) start_blink(face, now, 0);
    else if (timing[id].count) {
        face->phase = FACE_PHASE_ENTER;
        face->pose = timing[id].pose[0];
        face->frame_at_ms = now + timing[id].ms[0];
    }
    face->dirty = 1;
    return face_update(face); /* Paint first frame; never waits for later frames. */
}

int face_set_idle(face_t *face)
{
    return face_set(face, FACE_NEUTRAL);
}

static void advance_blink(face_t *face, uint64_t now)
{
    if (++face->blink_step == 4) {
        face->blinking = 0;
        face->current = FACE_NEUTRAL;
        face->phase = FACE_PHASE_HOLD;
        face->pose = 0;
        if (face->pending_second == 1) {
            face->pending_second = 2;
            face->blink_at_ms = now + DOUBLE_BLINK_GAP_MS;
        } else {
            face->pending_second = 0;
        }
    } else {
        face->step = face->blink_step;
        face->frame_at_ms = now + BLINK_FRAME_MS;
    }
    face->dirty = 1;
}

static void advance_micro(face_t *face, uint64_t now)
{
    unsigned next = 0;
    switch (face->current) {
        case FACE_HAPPY: next = face->pose ? 0 : 5; break;
        case FACE_SAD: next = face->pose ? 0 : 5; break;
        case FACE_WORRIED:
            if (face->pose == 4) { next = 0; face->step = 1; }
            else if (face->pose == 5) { next = 0; face->step = 0; }
            else next = face->step ? 5 : 4;
            break;
        case FACE_SLEEPY: next = face->pose < 4 || face->pose == 7 ? 4 : face->pose + 1; break;
        case FACE_ANGRY: next = face->pose ? 0 : 5; break;
        case FACE_AWE: next = face->pose == 5 ? 6 : 5; break;
        default: break;
    }
    face->pose = next;
    face->phase = next ? FACE_PHASE_MICRO : FACE_PHASE_HOLD;
    face->micro_at_ms = now + (face->current == FACE_WORRIED && !next && face->step == 1 ?
        WORRIED_LOOK_MS : next == 0 ? micro_wait(face) :
        face->current == FACE_HAPPY ? HAPPY_BOUNCE_MS :
        face->current == FACE_SAD ? SAD_SINK_MS :
        face->current == FACE_WORRIED ? WORRIED_LOOK_MS :
        face->current == FACE_ANGRY ? ANGRY_GLARE_MS :
        face->current == FACE_AWE ? AWE_SPARKLE_MS : SLEEPY_Z_MS);
    face->dirty = 1;
}

int face_update(face_t *face)
{
    if (!face || !face->oled || face->oled->fd < 0 || !face_name(face->current)) {
        errno = EINVAL; return -1;
    }
    uint64_t now;
    if (monotonic_ms(&now) < 0) return -1;

    if (face->phase == FACE_PHASE_BLINK && now >= face->frame_at_ms) {
        advance_blink(face, now);
    } else if (face->phase == FACE_PHASE_LOOK && now >= face->blink_at_ms &&
               face->blink_at_ms > face->look_at_ms) {
        face->pending_second = 0;
        face->look_at_ms = now + LOOK_MIN_MS + random_next(face) % LOOK_SPAN_MS;
        start_blink(face, now, 0);
    } else if (face->phase == FACE_PHASE_LOOK && now >= face->frame_at_ms) {
        ++face->step;
        face->pose = face->step == 1 || face->step == 3 ? 0 : 2;
        if (face->step == 4) {
            face->phase = FACE_PHASE_HOLD;
            face->pose = 0;
            face->look_at_ms = now + LOOK_MIN_MS + random_next(face) % LOOK_SPAN_MS;
        } else face->frame_at_ms = now + LOOK_STEP_MS;
        face->dirty = 1;
    } else if (face->phase == FACE_PHASE_ENTER && now >= face->frame_at_ms) {
        const face_timing_t *t = &timing[face->current];
        ++face->step;
        if (face->step >= t->count) { /* Guard against an invalid frame index. */
            face->step = 0;
            face->pose = 0;
            face->phase = FACE_PHASE_HOLD;
        } else {
            face->pose = t->pose[face->step];
            if (face->step + 1 == t->count) {
                face->phase = FACE_PHASE_HOLD;
                face->step = 0;
                unsigned wait = (unsigned)micro_wait(face);
                face->micro_at_ms = wait ? now + wait : 0;
            } else face->frame_at_ms = now + t->ms[face->step];
        }
        face->dirty = 1;
    } else if (face->phase == FACE_PHASE_HOLD && face->current == FACE_NEUTRAL) {
        if (now >= face->blink_at_ms) {
            if (face->pending_second == 2) face->pending_second = 0;
            else face->pending_second = 0;
            start_blink(face, now, 1);
        } else if (face->pending_second != 2 && now >= face->look_at_ms) {
            face->phase = FACE_PHASE_LOOK;
            face->step = 0;
            face->pose = 1;
            face->frame_at_ms = now + LOOK_STEP_MS;
            face->dirty = 1;
        }
    } else if ((face->phase == FACE_PHASE_HOLD || face->phase == FACE_PHASE_MICRO) &&
               face->current != FACE_NEUTRAL && face->micro_at_ms &&
               now >= face->micro_at_ms) {
        advance_micro(face, now);
    }

    if (!face->dirty) return 0;
    uint8_t frame[OLED_BUFFER_SIZE];
    eyes_image_t image = timing[face->current].image;
    unsigned pose = face->pose;
    if (face->phase == FACE_PHASE_BLINK) {
        static const eyes_image_t blink_images[4] = {
            EYES_NEUTRAL, EYES_HALF_CLOSED, EYES_CLOSED, EYES_HALF_CLOSED
        };
        image = blink_images[face->blink_step];
        pose = 0;
    }
    face_bitmap_render(&face->bitmaps, image, pose, frame);
    oled_clear(face->oled);
    if (oled_draw_bitmap(face->oled, 0, 0, frame, sizeof(frame),
                         OLED_WIDTH, OLED_HEIGHT) < 0 ||
        oled_display(face->oled) < 0) return -1;
    face->dirty = 0;
    return 0;
}

void face_close(face_t *face)
{
    if (!face) return;
    face->oled = NULL;
    face->dirty = 0;
}