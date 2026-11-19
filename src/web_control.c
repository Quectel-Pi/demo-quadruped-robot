#define _POSIX_C_SOURCE 200809L
#include "web_control.h"
#include "face.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* SIGUSR1 is delivered only to the worker, never to the HTTP thread. */
static volatile sig_atomic_t motion_stop;
static sigset_t stop_signal;
static sigset_t previous_mask;
static struct sigaction previous_handler;

static void stop_motion(int number)
{
    (void)number;
    motion_stop = 1;
}

static void drain_stop_signal(void)
{
    struct timespec zero = {0, 0};
    while (sigtimedwait(&stop_signal, NULL, &zero) == SIGUSR1) {}
    motion_stop = 0;
}

const char *web_state_name(web_state_t state)
{
    static const char *names[] = {
        "uninitialized", "initializing", "waiting-initialization", "idle",
        "running", "stopping", "restoring", "error"
    };
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "error";
}

const char *web_action_name(web_action_t action)
{
    static const char *names[] = {
        "stand", "forward", "backward", "turn-left", "turn-right",
        "hello-left", "hello-right", "dance", "dance2", "sit", "stop"
    };
    return (unsigned)action < sizeof(names) / sizeof(names[0]) ? names[action] : "stop";
}

static face_id_t face_for_action(web_action_t action)
{
    switch (action) {
    case WEB_FORWARD:
    case WEB_BACKWARD: return FACE_SURPRISED;
    case WEB_TURN_LEFT:
    case WEB_TURN_RIGHT: return FACE_AWE;
    case WEB_HELLO_LEFT:
    case WEB_HELLO_RIGHT:
    case WEB_DANCE:
    case WEB_DANCE2:
    case WEB_SIT: return FACE_HAPPY;
    default: return FACE_NEUTRAL; /* Stand. */
    }
}

static int run_motion(web_control_t *control, web_action_t action)
{
    movement_session_t *session = &control->session;
    const volatile sig_atomic_t never = 0;
    int result = 0;
    switch (action) {
    case WEB_FORWARD:
    case WEB_BACKWARD:
        /* Finish the current gait. An interrupted write also returns EINTR. */
        session->stopping = &never;
        while (!motion_stop) {
            result = action == WEB_FORWARD ? movement_forward(session) :
                                             movement_backward(session);
            if (result < 0) break;
        }
        break;
    case WEB_TURN_LEFT:
    case WEB_TURN_RIGHT:
    case WEB_HELLO_LEFT:
    case WEB_HELLO_RIGHT:
    case WEB_DANCE:
    case WEB_DANCE2:
    case WEB_SIT:
        /* These functions already restore on a graceful stop. */
        session->stopping = &never;
        if (action == WEB_TURN_LEFT)
            result = movement_turn_left_loop(session, &motion_stop);
        else if (action == WEB_TURN_RIGHT)
            result = movement_turn_right_loop(session, &motion_stop);
        else if (action == WEB_HELLO_LEFT)
            result = movement_hello_left_loop(session, &motion_stop);
        else if (action == WEB_DANCE)
            result = movement_dance_loop(session, &motion_stop);
        else if (action == WEB_DANCE2)
            result = movement_dance2_loop(session, &motion_stop);
        else if (action == WEB_SIT)
            result = movement_sit_loop(session, &motion_stop);
        else
            result = movement_hello_right_loop(session, &motion_stop);
        break;
    default: errno = EINVAL; return -1;
    }
    int error = errno;
    /* Hold SIGUSR1 during recovery, so a late signal cannot abort it. */
    int mask_error = pthread_sigmask(SIG_BLOCK, &stop_signal, NULL);
    if (mask_error) { errno = mask_error; return -1; }
    if (result < 0 && !(error == EINTR && motion_stop &&
                        session->known_mask == (1U << SERVO_COUNT) - 1U)) {
        errno = error;
        return -1; /* Never write blindly after I/O failure. */
    }
    if (session->known_mask != (1U << SERVO_COUNT) - 1U) {
        errno = EIO;
        return -1;
    }
    motion_stop = 0;
    /* Forward/backward do not restore themselves; the loop variants do.
       EINTR before a loop starts is the sole case requiring fallback recovery. */
    if (action == WEB_FORWARD || action == WEB_BACKWARD || result < 0) {
        pthread_mutex_lock(&control->mutex);
        control->status.state = WEB_RESTORING;
        pthread_mutex_unlock(&control->mutex);
        session->stopping = &never;
        return movement_restore_stand(session, 0, 0);
    }
    return 0;
}

static void *worker(void *opaque)
{
    web_control_t *control = opaque;
    const volatile sig_atomic_t never = 0;
    for (;;) {
        pthread_mutex_lock(&control->mutex);
        while (!control->closing &&
               !(control->status.state == WEB_UNINITIALIZED &&
                 control->requested == WEB_STAND) &&
               !(control->status.state == WEB_IDLE &&
                 control->requested != WEB_STOP))
            pthread_cond_wait(&control->wake, &control->mutex);
        if (control->closing) {
            pthread_mutex_unlock(&control->mutex);
            break;
        }
        if (control->status.state == WEB_UNINITIALIZED) {
            control->requested = WEB_STOP;
            control->status.state = WEB_INITIALIZING;
            control->status.action = WEB_STAND;
            pthread_mutex_unlock(&control->mutex);
            int result = servo_four_init(&control->servos);
            if (result == 0) {
                result = movement_session_init(&control->session, &control->servos.control, &never);
                if (result == 0) {
                    control->opened = 1; /* Only the worker reads/writes opened. */
                    result = movement_stand(&control->session, 400);
                } else {
                    int error = errno;
                    (void)servo_four_close(&control->servos);
                    errno = error;
                }
            }
            int error = errno;
            pthread_mutex_lock(&control->mutex);
            control->status.state = result == 0 ? WEB_IDLE : WEB_ERROR;
            control->status.action = WEB_STOP;
            control->status.error = result == 0 ? 0 : error;
            pthread_mutex_unlock(&control->mutex);
            continue;
        }
        web_action_t action = control->requested;
        drain_stop_signal(); /* Signals remain blocked outside the running action. */
        control->status.state = WEB_RUNNING;
        control->status.action = action;
        int mask_error = pthread_sigmask(SIG_UNBLOCK, &stop_signal, NULL);
        if (mask_error) {
            control->status.state = WEB_ERROR;
            control->status.error = mask_error;
            control->requested = WEB_STOP;
            pthread_mutex_unlock(&control->mutex);
            continue;
        }
        atomic_store(&control->face_target, face_for_action(action));
        pthread_mutex_unlock(&control->mutex);
        int result = run_motion(control, action);
        int error = errno;
        pthread_mutex_lock(&control->mutex);
        if (result == 0 && control->requested == WEB_STOP)
            atomic_store(&control->face_target, control->stop_face);
        else if (result < 0)
            atomic_store(&control->face_target, FACE_NEUTRAL);
        control->status.state = result == 0 ? WEB_IDLE : WEB_ERROR;
        control->status.action = WEB_STOP;
        control->status.error = result == 0 ? 0 : error;
        if (result < 0) control->requested = WEB_STOP;
        pthread_mutex_unlock(&control->mutex);
    }
    if (control->opened && servo_four_close(&control->servos) < 0)
        fprintf(stderr, "web: servo close failed: %s\n", strerror(errno));
    return NULL;
}

int web_control_init(web_control_t *control)
{
    memset(control, 0, sizeof(*control));
    atomic_init(&control->face_target, FACE_NEUTRAL);
    control->stop_face = FACE_SLEEPY;
    control->servos.driver.fd = -1;
    control->requested = WEB_STOP;
    control->status.state = WEB_UNINITIALIZED;
    control->status.action = WEB_STOP;
    control->status.pending = WEB_STOP;
    if (sigemptyset(&stop_signal) < 0 || sigaddset(&stop_signal, SIGUSR1) < 0)
        return -1;
    int error = pthread_sigmask(SIG_BLOCK, &stop_signal, &previous_mask);
    if (error) { errno = error; return -1; }
    struct sigaction handler = {0};
    handler.sa_handler = stop_motion;
    if (sigemptyset(&handler.sa_mask) < 0 ||
        sigaction(SIGUSR1, &handler, &previous_handler) < 0) {
        int saved = errno;
        (void)pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
        errno = saved;
        return -1;
    }
    error = pthread_mutex_init(&control->mutex, NULL);
    if (error) goto fail_handler;
    error = pthread_cond_init(&control->wake, NULL);
    if (error) goto fail_mutex;
    error = pthread_create(&control->thread, NULL, worker, control);
    if (!error) return 0;
    pthread_cond_destroy(&control->wake);
fail_mutex:
    pthread_mutex_destroy(&control->mutex);
fail_handler:
    (void)sigaction(SIGUSR1, &previous_handler, NULL);
    (void)pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
    errno = error;
    return -1;
}

int web_control_request(web_control_t *control, web_action_t action)
{
    pthread_mutex_lock(&control->mutex);
    int result = 0;
    /* Stand during the held sit uses the same graceful stop/recovery path. */
    if (action == WEB_STAND && !control->closing &&
        control->status.state == WEB_RUNNING &&
        control->status.action == WEB_SIT) {
        control->stop_face = FACE_NEUTRAL;
        action = WEB_STOP;
    }
    if (control->closing || control->status.state == WEB_ERROR ||
        (unsigned)action > WEB_STOP) result = -2;
    else if (action == WEB_STAND) {
        if (control->status.state == WEB_UNINITIALIZED) {
            control->requested = WEB_STAND;
            pthread_cond_signal(&control->wake);
        } else if (control->status.state == WEB_IDLE) {
            atomic_store(&control->face_target, FACE_NEUTRAL);
        } /* Already initialized: do not repeat the stand command. */
    } else if (action != WEB_STOP &&
               (control->status.state == WEB_UNINITIALIZED ||
                control->status.state == WEB_INITIALIZING ||
                control->status.state == WEB_INIT_STOPPING)) result = -1;
    else {
        control->requested = action; /* One desired action, not a request queue. */
        if (action == WEB_STOP) {
            if (control->status.state == WEB_IDLE)
                atomic_store(&control->face_target, FACE_SLEEPY);
            else if (control->status.state == WEB_RUNNING ||
                     control->status.state == WEB_STOPPING ||
                     control->status.state == WEB_RESTORING)
                control->stop_face = FACE_SLEEPY;
        }
        if (control->status.state == WEB_INITIALIZING && action == WEB_STOP)
            control->status.state = WEB_INIT_STOPPING;
        if (control->status.state == WEB_RUNNING &&
            action != control->status.action) {
            control->status.state = WEB_STOPPING;
            int error = pthread_kill(control->thread, SIGUSR1);
            if (error) { control->status.state = WEB_ERROR; control->status.error = error;
                         control->requested = WEB_STOP; result = -2; }
        }
        pthread_cond_signal(&control->wake);
    }
    pthread_mutex_unlock(&control->mutex);
    return result;
}

web_status_t web_control_status(web_control_t *control)
{
    pthread_mutex_lock(&control->mutex);
    web_status_t status = control->status;
    status.pending = control->requested;
    pthread_mutex_unlock(&control->mutex);
    return status;
}

void web_control_close(web_control_t *control)
{
    pthread_mutex_lock(&control->mutex);
    control->closing = 1;
    control->requested = WEB_STOP;
    if (control->status.state == WEB_RUNNING)
        (void)pthread_kill(control->thread, SIGUSR1);
    pthread_cond_signal(&control->wake);
    pthread_mutex_unlock(&control->mutex);
    pthread_join(control->thread, NULL);
    pthread_cond_destroy(&control->wake);
    pthread_mutex_destroy(&control->mutex);
    (void)pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
    (void)sigaction(SIGUSR1, &previous_handler, NULL);
}
