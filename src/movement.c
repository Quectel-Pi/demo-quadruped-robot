#include "movement.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define MOTION_TICK_MS 20U
#define ALL_KNOWN ((1U << SERVO_COUNT) - 1U)

/* CH0..CH7 logical angles; servo_control.c owns mirroring and calibration. */
static const int stand_pose[SERVO_COUNT] = {60, 120, 60, 120, 140, 140, 140, 140};

static int validate_pose(const int angles[SERVO_COUNT])
{
    if (!angles) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < SERVO_COUNT; ++i) {
        if (angles[i] < 0 || angles[i] > 180) {
            errno = EINVAL; return -1;
        }
    }
    return 0;
}

static int ready(movement_session_t *session)
{
    if (!session || !session->control || !session->control->driver ||
        session->control->driver->fd < 0 || !session->stopping) {
        errno = EINVAL; return -1;
    }
    if (*session->stopping) { errno = EINTR; return -1; }
    return 0;
}

int movement_session_init(movement_session_t *session, servo_control_t *control,
                          const volatile sig_atomic_t *stopping)
{
    if (!session || !control || !control->driver || control->driver->fd < 0 || !stopping) {
        errno = EINVAL; return -1;
    }
    *session = (movement_session_t){.control = control, .stopping = stopping};
    return 0;
}

int movement_confirm_start(movement_session_t *session, const int confirmed[SERVO_COUNT])
{
    if (!session || !session->control || !session->control->driver ||
        session->control->driver->fd < 0 || !session->stopping ||
        validate_pose(confirmed) < 0) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < SERVO_COUNT; ++i) {
        if (!session->control->calibration[i].calibrated ||
            session->control->calibration[i].direction != 1) {
            errno = EPERM; return -1;
        }
    }
    memcpy(session->last_commanded, confirmed, sizeof(session->last_commanded));
    session->known_mask = ALL_KNOWN;
    return 0;
}

static int64_t millis_between(const struct timespec *start, const struct timespec *end)
{
    return ((int64_t)end->tv_sec - (int64_t)start->tv_sec) * 1000 +
           ((int64_t)end->tv_nsec - (int64_t)start->tv_nsec) / 1000000;
}

/* Absolute monotonic deadlines; check cancellation at least every 10 ms. */
static int wait_until(const struct timespec *start, unsigned elapsed_ms,
                      const volatile sig_atomic_t *stopping)
{
    struct timespec deadline = *start;
    deadline.tv_sec += elapsed_ms / 1000U;
    deadline.tv_nsec += (long)(elapsed_ms % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        ++deadline.tv_sec;
    }
    for (;;) {
        if (*stopping) { errno = EINTR; return -1; }
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
        int64_t left_ns = ((int64_t)deadline.tv_sec - (int64_t)now.tv_sec) * 1000000000 +
                          deadline.tv_nsec - now.tv_nsec;
        if (left_ns <= 0) return 0;
#ifdef _WIN32
        Sleep((DWORD)(left_ns >= 10000000 ? 10 : 1));
#else
        struct timespec slice = now;
        int64_t ns = left_ns > 10000000 ? 10000000 : left_ns;
        slice.tv_nsec += (long)ns;
        if (slice.tv_nsec >= 1000000000L) {
            slice.tv_nsec -= 1000000000L;
            ++slice.tv_sec;
        }
        int err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &slice, NULL);
        if (err && err != EINTR) { errno = err; return -1; }
#endif
    }
}

static int write_channels(movement_session_t *session, const int angles[SERVO_COUNT],
                          unsigned first, unsigned end)
{
    for (unsigned i = first; i < end; ++i) {
        if (*session->stopping) {
            session->known_mask = 0; /* Mechanical progress is unknown after interruption. */
            errno = EINTR; return -1;
        }
        if (servo_set_leg_angle(session->control, (servo_id_t)i, angles[i]) < 0) {
            /* I2C failure may mean an unknown partial write: do not resume. */
            session->known_mask = 0;
            return -1;
        }
        session->last_commanded[i] = angles[i];
    }
    return 0;
}

static int play_frame(movement_session_t *session, const motion_frame_t *frame,
                      unsigned first, unsigned end)
{
    if (session->known_mask != ALL_KNOWN) {
        /* Unknown start: write targets directly, never interpolate a guessed pose. */
        if (write_channels(session, frame->angles, first, end) < 0) return -1;
        if (first == 0 && end == SERVO_COUNT) session->known_mask = ALL_KNOWN;
        return 0;
    }
    int from[SERVO_COUNT];
    memcpy(from, session->last_commanded, sizeof(from));
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) return -1;
    unsigned next_ms = MOTION_TICK_MS;
    for (;;) {
        unsigned due_ms = next_ms < frame->duration_ms ? next_ms : frame->duration_ms;
        if (wait_until(&start, due_ms, session->stopping) < 0) {
            session->known_mask = 0;
            return -1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
        int64_t elapsed = millis_between(&start, &now);
        if (elapsed < (int64_t)due_ms) elapsed = due_ms;
        if (elapsed > (int64_t)frame->duration_ms) elapsed = frame->duration_ms;
        double progress = (double)elapsed / frame->duration_ms;
        int values[SERVO_COUNT];
        if (elapsed == frame->duration_ms) {
            memcpy(values, frame->angles, sizeof(values));
        } else {
            for (unsigned i = first; i < end; ++i) {
                double target = from[i] + progress * (frame->angles[i] - from[i]);
                values[i] = (int)(target + 0.5);
            }
        }
        if (write_channels(session, values, first, end) < 0) return -1;
        if (elapsed == frame->duration_ms) break;
        unsigned slot = (unsigned)elapsed / MOTION_TICK_MS + 1U;
        next_ms = slot > UINT32_MAX / MOTION_TICK_MS ? frame->duration_ms :
                  slot * MOTION_TICK_MS;
    }
    return 0;
}

/* Reuse the same frame validation, interpolation and cancellation for a channel range.
 * The public movement_play() still writes all eight channels as before. */
static int movement_play_channels(movement_session_t *session,
                                  const motion_frame_t *frames, size_t count,
                                  unsigned first, unsigned end)
{
    if (!frames || !count || first >= end || end > SERVO_COUNT) {
        errno = EINVAL; return -1;
    }
    /* Reject the entire plan before the first PWM write. */
    for (size_t f = 0; f < count; ++f) {
        if (!frames[f].duration_ms || validate_pose(frames[f].angles) < 0) {
            errno = EINVAL; return -1;
        }
    }
    if (ready(session) < 0) return -1;
    for (size_t f = 0; f < count; ++f) {
        if (play_frame(session, &frames[f], first, end) < 0) return -1;
    }
    return 0;
}

int movement_play(movement_session_t *session, const motion_frame_t *frames, size_t count)
{
    return movement_play_channels(session, frames, count, 0, SERVO_COUNT);
}

int movement_stand(movement_session_t *session, unsigned duration_ms)
{
    motion_frame_t stand = {.duration_ms = duration_ms};
    memcpy(stand.angles, stand_pose, sizeof(stand.angles));
    return movement_play(session, &stand, 1);
}

int movement_restore_stand(movement_session_t *session,
                           unsigned upper_ms, unsigned lower_ms)
{
    enum { RESTORE_UPPER_MS = 850, RESTORE_LOWER_MS = 750 };
    if (ready(session) < 0) return -1;
    if (!upper_ms) upper_ms = RESTORE_UPPER_MS;
    if (!lower_ms) lower_ms = RESTORE_LOWER_MS;
    if (session->known_mask != ALL_KNOWN) {
        /* No known full starting pose: write each group directly in order. */
        if (write_channels(session, stand_pose, SERVO_L3, SERVO_COUNT) < 0 ||
            write_channels(session, stand_pose, SERVO_L1, SERVO_L3) < 0) return -1;
        session->known_mask = ALL_KNOWN; /* Command targets, not measured angles. */
        return 0;
    }
    motion_frame_t upper = {.duration_ms = upper_ms};
    memcpy(upper.angles, session->last_commanded, sizeof(upper.angles));
    for (unsigned i = SERVO_L3; i < SERVO_COUNT; ++i)
        upper.angles[i] = stand_pose[i];
    if (play_frame(session, &upper, SERVO_L3, SERVO_COUNT) < 0) return -1;
    motion_frame_t lower = {.duration_ms = lower_ms};
    memcpy(lower.angles, stand_pose, sizeof(lower.angles));
    return play_frame(session, &lower, SERVO_L1, SERVO_L3);
}

int movement_rest(movement_session_t *session)
{
    (void)session; errno = ENOSYS; return -1;
}

/* 八路顺序：L1、L2、R1、R2、L3、L4、R3、R4；每行是一个动作阶段。
 * 修改各行八路目标角度可调步态；修改 duration_ms 可调阶段速度。
 * 本步态上部 120° 为抬腿目标、140° 为站姿目标；底部目标角度仍需实机调整。
 * 以下只是软件目标，不能保证真实机械位置或负载稳定性。 */
static const motion_frame_t forward_frames[] = {

    /* 1. A组抬腿：L3、R4 从140°到120°，左前腿、右后腿抬起 */
    {{60, 120, 60, 120, 120, 140, 140, 120}, .duration_ms = 100},

    /* 2. A组前摆30°：L1 60→30、R2 120→90；B组接地后摆30°：L2 120→150、R1 60→90 */
    {{30, 150, 90, 90, 120, 140, 140, 120}, .duration_ms = 100},

    /* 3. A组落腿：L3、R4 从120°到140°，底部角度保持不变 */
    {{30, 150, 90, 90, 140, 140, 140, 140}, .duration_ms = 100},

    /* 4. B组抬腿：L4、R3 从140°到120°，左后腿、右前腿抬起 */
    {{30, 150, 90, 90, 140, 120, 120, 140}, .duration_ms = 100},

    /* 5. B组前摆30°：L2 150→120、R1 90→60；A组接地后摆30°：L1 30→60、R2 90→120 */
    {{60, 120, 60, 120, 140, 120, 120, 140}, .duration_ms = 100},

    /* 6. B组落腿：L4、R3 从120°到140°，四脚接地并恢复标准站姿 */
    {{60, 120, 60, 120, 140, 140, 140, 140}, .duration_ms = 100}
};

int movement_forward(movement_session_t *session)
{
    return movement_play(session, forward_frames,
                         sizeof(forward_frames) / sizeof(forward_frames[0]));
}

/* First mechanical right-turn trial: diagonal lift, swing, land, then grounded return. */
static const motion_frame_t turn_right_frames[] = {
    /* 1. 抬起左前、右后 */
    {{60,120,60,120,120,140,140,120}, .duration_ms = 100},

    /* 2. 左前向前 60°，右后向后 60° */
    {{0,120,60,180,120,140,140,120}, .duration_ms = 100},

    /* 3. 放下左前、右后 */
    {{0,120,60,180,140,140,140,140}, .duration_ms = 100},

    /* 4. 抬起右前、左后 */
    {{0,120,60,180,140,120,120,140}, .duration_ms = 100},

    /* 5. 右前向后 60°，左后向前 60° */
    {{0,60,120,180,140,120,120,140}, .duration_ms = 100},

    /* 6. 放下右前、左后 */
    {{0,60,120,180,140,140,140,140}, .duration_ms = 100},

    /* 7. 四脚接地，回摆产生右转 */
    {{60,120,60,120,140,140,140,140}, .duration_ms = 100}
};

int movement_turn_right_loop(movement_session_t *session,
                             const volatile sig_atomic_t *stop_requested)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    for (;;) {
        for (size_t i = 0; i < sizeof(turn_right_frames) / sizeof(turn_right_frames[0]); ++i) {
            if (*stop_requested) return movement_restore_stand(session, 0, 0);
            if (movement_play(session, &turn_right_frames[i], 1) < 0) return -1;
        }
    }
}

static const motion_frame_t turn_left_frames[] = {
    /* 1. 抬起右前、左后 */
    {{60,120,60,120,140,120,120,140}, .duration_ms = 100},

    /* 2. 右前向前 60°，左后向后 60° */
    {{60,180,0,120,140,120,120,140}, .duration_ms = 100},

    /* 3. 放下右前、左后 */
    {{60,180,0,120,140,140,140,140}, .duration_ms = 100},

    /* 4. 抬起左前、右后 */
    {{60,180,0,120,120,140,140,120}, .duration_ms = 100},

    /* 5. 左前向后 60°，右后向前 60° */
    {{120,180,0,60,120,140,140,120}, .duration_ms = 100},

    /* 6. 放下左前、右后 */
    {{120,180,0,60,140,140,140,140}, .duration_ms = 100},

    /* 7. 四脚接地，回摆产生左转 */
    {{60,120,60,120,140,140,140,140}, .duration_ms = 100}
};

int movement_turn_left_loop(movement_session_t *session,
                            const volatile sig_atomic_t *stop_requested)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    for (;;) {
        for (size_t i = 0; i < sizeof(turn_left_frames) / sizeof(turn_left_frames[0]); ++i) {
            if (*stop_requested) return movement_restore_stand(session, 0, 0);
            if (movement_play(session, &turn_left_frames[i], 1) < 0) return -1;
        }
    }
}

/* 八路顺序：L1、L2、R1、R2、L3、L4、R3、R4。 */
static const motion_frame_t hello_right_frames[] = {
    /* 准备姿态 */
    {{40,120,10,120,140,60,0,140}, .duration_ms = 500},
    /* R3 向下、向上循环挥动 */
    {{40,120,10,120,140,60,50,140}, .duration_ms = 350},
    {{40,120,10,120,140,60,0,140}, .duration_ms = 350},
};

/* 顺序：L1, L2, R1, R2, L3, L4, R3, R4。 */
static const motion_frame_t hello_left_frames[] = {
    /* 准备姿态 */
    {{10,120,40,120,0,140,140,60}, .duration_ms = 500},
    /* L3 向下、向上循环挥动 */
    {{10,120,40,120,50,140,140,60}, .duration_ms = 350},
    {{10,120,40,120,0,140,140,60}, .duration_ms = 350},
};

/* stop_requested is a graceful request, distinct from session->stopping (abort).
 * Finish the current two-frame wave before restoring; never restore after I/O failure. */
static int hello_loop(movement_session_t *session,
                      const volatile sig_atomic_t *stop_requested,
                      const motion_frame_t *frames,
                      unsigned restore_upper_ms, unsigned restore_lower_ms)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    if (movement_play(session, frames, 1) < 0) return -1;
    while (!*stop_requested) {
        if (movement_play(session, frames + 1, 2) < 0) return -1;
    }
    return movement_restore_stand(session, restore_upper_ms, restore_lower_ms);
}

int movement_hello_right_loop(movement_session_t *session,
                              const volatile sig_atomic_t *stop_requested)
{
    return hello_loop(session, stop_requested, hello_right_frames, 0, 0);
}

int movement_hello_left_loop(movement_session_t *session,
                             const volatile sig_atomic_t *stop_requested)
{
    return hello_loop(session, stop_requested, hello_left_frames, 700, 350);
}

static const motion_frame_t backward_frames[] = {
    /* 1. A组抬腿：左前、右后 */
    {{60,120,60,120,120,140,140,120}, .duration_ms = 100},

    /* 2. A组后摆30°，B组接地前摆30° */
    {{90,90,30,150,120,140,140,120}, .duration_ms = 100},

    /* 3. A组落腿 */
    {{90,90,30,150,140,140,140,140}, .duration_ms = 100},

    /* 4. B组抬腿：左后、右前 */
    {{90,90,30,150,140,120,120,140}, .duration_ms = 100},

    /* 5. B组后摆30°，A组接地前摆30° */
    {{60,120,60,120,140,120,120,140}, .duration_ms = 100},

    /* 6. B组落腿，恢复标准站姿 */
    {{60,120,60,120,140,140,140,140}, .duration_ms = 100}
};

int movement_backward(movement_session_t *session)
{
    return movement_play(session, backward_frames,
                         sizeof(backward_frames) / sizeof(backward_frames[0]));
}

static const motion_frame_t sit_frames[] = {
    /* 1. 底部四舵机摆动到坐姿目标 */
    {{0,180,0,180,140,140,140,140}, .duration_ms = 300},

    /* 2. 上层舵机弯曲，完成坐姿 */
    {{0,180,0,180,120,80,120,80}, .duration_ms = 300}
};

int movement_sit(movement_session_t *session)
{
    return movement_play(session, sit_frames,
                         sizeof(sit_frames) / sizeof(sit_frames[0]));
}

int movement_sit_loop(movement_session_t *session,
                      const volatile sig_atomic_t *stop_requested)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    if (movement_sit(session) < 0) return -1;
    while (!*stop_requested) {
        struct timespec start;
        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) return -1;
        if (wait_until(&start, 1000U, stop_requested) < 0) {
            if (errno != EINTR || !*stop_requested) return -1;
            break;
        }
    }
    if (session->known_mask != ALL_KNOWN) { errno = EIO; return -1; }
    return movement_restore_stand(session, 0, 0);
}

static const motion_frame_t dance_frames[] = {
     /* 1. 准备姿态：底部90°，上部140°，仅执行一次 */
    {{90,90,90,90,140,140,140,140}, .duration_ms = 200},

    /* 2. 右侧抬起：R3、R4转到100° */
    {{90,90,90,90,140,140,100,100}, .duration_ms = 250},

    /* 3. 左侧抬起，右侧恢复：L3、L4转到100°，R3、R4恢复140° */
    {{90,90,90,90,100,100,140,140}, .duration_ms = 250}
};

_Static_assert(sizeof(dance_frames) / sizeof(dance_frames[0]) == 3,
               "dance requires exactly three frames");

int movement_dance_loop(movement_session_t *session,
                        const volatile sig_atomic_t *stop_requested)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    /* A known start is required for the specified 400/500 ms interpolated frames. */
    if (session->known_mask != ALL_KNOWN) { errno = EIO; return -1; }
    if (movement_play(session, dance_frames, 1) < 0) return -1;
    for (size_t i = 1; ; i = i == 1 ? 2 : 1) {
        if (*stop_requested) break;
        /* Existing frame interpolation updates both upper sides in the same tick. */
        if (movement_play_channels(session, &dance_frames[i], 1,
                                   SERVO_L3, SERVO_COUNT) < 0)
            return -1;
    }
    if (session->known_mask != ALL_KNOWN) { errno = EIO; return -1; }
    return movement_restore_stand(session, 0, 0);
}

static const motion_frame_t dance2_frames[] = {
    /* 1. 四条腿同时抬起 */
    {{60,120,60,120,100,100,100,100}, .duration_ms = 200},

    /* 2. 四条腿同时恢复 */
    {{60,120,60,120,140,140,140,140}, .duration_ms = 200}
};

int movement_dance2_loop(movement_session_t *session,
                         const volatile sig_atomic_t *stop_requested)
{
    if (!stop_requested) { errno = EINVAL; return -1; }
    if (ready(session) < 0) return -1;
    if (*stop_requested) { errno = EINTR; return -1; }
    if (session->known_mask != ALL_KNOWN) { errno = EIO; return -1; }
    for (size_t i = 0; ; i ^= 1U) {
        if (*stop_requested) break;
        if (movement_play_channels(session, &dance2_frames[i], 1,
                                   SERVO_L3, SERVO_COUNT) < 0)
            return -1;
    }
    if (session->known_mask != ALL_KNOWN) { errno = EIO; return -1; }
    return movement_restore_stand(session, 0, 0);
}
