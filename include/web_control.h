#ifndef WEB_CONTROL_H
#define WEB_CONTROL_H

#include "movement.h"
#include <pthread.h>
#include <stdatomic.h>

typedef enum {
    WEB_STAND, WEB_FORWARD, WEB_BACKWARD, WEB_TURN_LEFT,
    WEB_TURN_RIGHT, WEB_HELLO_LEFT, WEB_HELLO_RIGHT, WEB_DANCE, WEB_DANCE2, WEB_SIT, WEB_STOP
} web_action_t;

typedef enum { WEB_UNINITIALIZED, WEB_INITIALIZING, WEB_INIT_STOPPING, WEB_IDLE,
               WEB_RUNNING, WEB_STOPPING, WEB_RESTORING, WEB_ERROR } web_state_t;

typedef struct {
    web_state_t state;
    web_action_t action;
    web_action_t pending;
    int error;
} web_status_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    int closing;
    int opened;
    web_action_t requested;
    web_status_t status;
    atomic_int face_target;
    int stop_face;
    servo_four_t servos;
    movement_session_t session;
} web_control_t;

/* A single controller per process; all servo access belongs to its worker. */
int web_control_init(web_control_t *control);
/* Returns 0 accepted, -1 stand required, -2 error/closing. */
int web_control_request(web_control_t *control, web_action_t action);
web_status_t web_control_status(web_control_t *control);
void web_control_close(web_control_t *control);
const char *web_state_name(web_state_t state);
const char *web_action_name(web_action_t action);

#endif
