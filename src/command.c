#include "command.h"
#include "face.h"
#include "movement.h"
#include "oled.h"
#include "servo_control.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORWARD_SETTLE_MS 2000

static int run_stand(const volatile sig_atomic_t *stopping)
{
    servo_four_t servos = {.driver = {.fd = -1}};
    movement_session_t session;
    int result = 1;
    fprintf(stderr, "stand: 起点未知，将直接下发八路站姿目标；可能产生较大动作，非到位检测。\n");
    if (*stopping) return 1;
    if (servo_four_init(&servos) < 0) {
        perror("stand: PCA9685/servo init");
        return 1;
    }
    if (movement_session_init(&session, &servos.control, stopping) < 0 ||
        movement_stand(&session, 400) < 0) {
        perror("stand: PWM write");
    } else {
        result = 0;
    }
    if (servo_four_close(&servos) < 0) {
        perror("stand: PCA9685 close");
        result = 1;
    }
    return result;
}

static int run_forward(const volatile sig_atomic_t *stopping, int loop, int hello)
{
    servo_four_t servos = {.driver = {.fd = -1}};
    int result = 1;
    const char *name = hello == 3 ? "hello-right-loop" :
                       hello == 4 ? "hello-left-loop" :
                       hello == 6 ? "turn-right-loop" :
                       hello == 8 ? "turn-left-loop" :
                       hello == 9 ? (loop ? "backward-loop" : "backward") :
                       hello == 10 ? "sit" :
                       hello == 11 ? "dance-loop" :
                       hello == 12 ? "dance2-loop" : "forward";

    fprintf(stderr, "%s: 首次站姿将直接下发八路目标，可能产生较大动作；请先确保周围安全。\n"
                "%s: 随后固定等待 %d ms，仅用于预留运动时间，不是实际到位检测。\n",
            name, name, FORWARD_SETTLE_MS);
    if (*stopping) return 1;
    if (servo_four_init(&servos) < 0) {
        perror(hello == 3 ? "hello-right-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 4 ? "hello-left-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 6 ? "turn-right-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 8 ? "turn-left-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 9 ? "backward: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 10 ? "sit: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 11 ? "dance-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
              hello == 12 ? "dance2-loop: PCA9685/servo init (/dev/i2c-7, 0x40)" :
                           "forward: PCA9685/servo init (/dev/i2c-7, 0x40)");
        return 1;
    }
    movement_session_t session;
    volatile sig_atomic_t motion_abort = 0;
    if (movement_session_init(&session, &servos.control, stopping) < 0 ||
        movement_stand(&session, 400) < 0) goto fail;
    for (int elapsed = 0; elapsed < FORWARD_SETTLE_MS; elapsed += 100) {
        if (*stopping) { errno = EINTR; goto fail; }
        if (poll(NULL, 0, 100) < 0) {
            if (errno == EINTR && !*stopping) { elapsed -= 100; continue; }
            goto fail;
        }
    }
    if (*stopping) { errno = EINTR; goto fail; }
    if (hello == 11) {
        session.stopping = &motion_abort; /* Finish a phase and recover on Ctrl+C. */
        if (movement_dance_loop(&session, stopping) < 0) goto fail;
        result = 0;
        goto done;
    }
    if (hello == 12) {
        session.stopping = &motion_abort;
        if (movement_dance2_loop(&session, stopping) < 0) goto fail;
        result = 0;
        goto done;
    }
    if (hello == 6) {
        session.stopping = &motion_abort; /* Ctrl+C finishes the current frame. */
        if (movement_turn_right_loop(&session, stopping) < 0) goto fail;
        result = 0;
        goto done;
    }
    if (hello == 8) {
        session.stopping = &motion_abort;
        if (movement_turn_left_loop(&session, stopping) < 0) goto fail;
        result = 0;
        goto done;
    }
    if (hello == 3 || hello == 4) {
        /* Graceful loop stop must not interrupt its interpolated recovery. */
        session.stopping = &motion_abort;
        if ((hello == 3 ? movement_hello_right_loop(&session, stopping) :
                          movement_hello_left_loop(&session, stopping)) < 0) goto fail;
        result = 0;
        goto done;
    }
    if (hello == 10) {
        if (movement_sit(&session) < 0) {
            if (!*stopping || errno != EINTR) goto fail;
        } else {
            /* Keep PWM active without repeating the sit frames or writing angles. */
            while (!*stopping) {
                if (poll(NULL, 0, 100) < 0 && errno != EINTR) goto fail;
            }
        }
        session.stopping = &motion_abort; /* Ctrl+C must not abort recovery. */
        if (movement_restore_stand(&session, 0, 0) < 0) goto fail;
        result = 0;
        goto done;
    }
    do {
        int action = hello == 9 ? movement_backward(&session) : movement_forward(&session);
        if (action < 0) {
            if (loop && *stopping && errno == EINTR) {
                session.stopping = &motion_abort; /* Finish recovery after Ctrl+C. */
                if (movement_restore_stand(&session, 0, 0) < 0) goto fail;
                break;
            }
            goto fail;
        }
    } while (loop && !*stopping);
    result = 0;
    goto done;
fail:
    perror((hello == 3 || hello == 4) ? "hello-loop: stand/settle/action failed (recovery not guaranteed)" :
          hello == 6 ? "turn-right-loop: stand/settle/action failed (recovery not guaranteed)" :
          hello == 8 ? "turn-left-loop: stand/settle/action failed (recovery not guaranteed)" :
          hello == 9 ? "backward: stand/settle/gait failed (no further PWM commands)" :
          hello == 10 ? "sit: stand/settle/action/recovery failed" :
          hello == 11 ? "dance-loop: stand/settle/action/recovery failed" :
          hello == 12 ? "dance2-loop: stand/settle/action/recovery failed" :
                       "forward: stand/settle/gait failed (no further PWM commands)");
done:
    if (servo_four_close(&servos) < 0) {
        perror(hello == 3 ? "hello-right-loop: PCA9685 close" :
              hello == 4 ? "hello-left-loop: PCA9685 close" :
              hello == 6 ? "turn-right-loop: PCA9685 close" :
              hello == 8 ? "turn-left-loop: PCA9685 close" :
              hello == 9 ? "backward: PCA9685 close" :
              hello == 10 ? "sit: PCA9685 close" :
              hello == 11 ? "dance-loop: PCA9685 close" :
              hello == 12 ? "dance2-loop: PCA9685 close" : "forward: PCA9685 close");
        result = 1;
    }
    return result;
}

static int parse_face(const char *name, face_id_t *id)
{
    for (int i = 0; i < FACE_COUNT; ++i) {
        if (strcmp(name, face_name((face_id_t)i)) == 0) {
            *id = (face_id_t)i;
            return 0;
        }
    }
    return -1;
}

static int parse_bottom_servo(const char *name, servo_id_t *id)
{
    for (unsigned ch = 0; ch < SERVO_COUNT; ++ch) {
        if (strcmp(name, servo_bottom_name((servo_id_t)ch)) == 0) {
            *id = (servo_id_t)ch;
            return 0;
        }
    }
    return -1;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [face list | face <name> [i2c-device]]\n"
                    "       %s servo <0..180> | servo-test (CH0 only)\n"
                    "       %s servo-center | servo <L1|L2|R1|R2|L3|L4|R3|R4> <0..180>\n"
                    "       %s leg <L1|L2|R1|R2|L3|L4|R3|R4> <angle> (CH0..CH7: 0..180 logical)\n"
                    "       %s legs <L1> <L2> <R1> <R2> (logical angles 0..180)\n"
                    "       %s legs-upper <L3> <L4> <R3> <R4> (logical angles 0..180)\n"
                    "       %s stand | sit | forward | forward-loop | backward | backward-loop | turn-right-loop | turn-left-loop\n"
                    "       %s hello-right-loop | hello-left-loop | dance-loop | dance2-loop (Ctrl+C restores stand)\n"
                    "With no arguments, switch faces interactively; default device is /dev/i2c-3.\n",
            program,
            program,
        program,
            program,
            program,
            program,
            program,
            program);
}

int command_run(int argc, char **argv, const volatile sig_atomic_t *stopping)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stand") == 0)
        return run_stand(stopping);
    if (argc == 2 && strcmp(argv[1], "sit") == 0)
        return run_forward(stopping, 0, 10);
    if (argc == 2 && strcmp(argv[1], "forward") == 0) {
        return run_forward(stopping, 0, 0);
    }
    if (argc == 2 && strcmp(argv[1], "forward-loop") == 0)
        return run_forward(stopping, 1, 0);
    if (argc == 2 && strcmp(argv[1], "backward") == 0)
        return run_forward(stopping, 0, 9);
    if (argc == 2 && strcmp(argv[1], "backward-loop") == 0)
        return run_forward(stopping, 1, 9);
    if (argc == 2 && strcmp(argv[1], "turn-right-loop") == 0)
        return run_forward(stopping, 0, 6);
    if (argc == 2 && strcmp(argv[1], "turn-left-loop") == 0)
        return run_forward(stopping, 0, 8);
    if (argc == 2 && strcmp(argv[1], "hello-right-loop") == 0)
        return run_forward(stopping, 0, 3);
    if (argc == 2 && strcmp(argv[1], "hello-left-loop") == 0)
        return run_forward(stopping, 0, 4);
    if (argc == 2 && strcmp(argv[1], "dance-loop") == 0)
        return run_forward(stopping, 0, 11);
    if (argc == 2 && strcmp(argv[1], "dance2-loop") == 0)
        return run_forward(stopping, 0, 12);
    if (argc == 2 && strcmp(argv[1], "servo-test") == 0)
        return servo_run_single(1, 0, stopping);
    if (argc == 2 && strcmp(argv[1], "servo-center") == 0)
        return servo_run_bottom(1, 0, SERVO_L1, 90, stopping);
    if (argc == 6 && (strcmp(argv[1], "legs") == 0 || strcmp(argv[1], "legs-upper") == 0)) {
        int upper = strcmp(argv[1], "legs-upper") == 0;
        int angles[4];
        for (int i = 0; i < 4; ++i) {
            char *end;
            errno = 0;
            long value = strtol(argv[i + 2], &end, 10);
            if (errno || end == argv[i + 2] || *end != '\0' || value < 0 ||
                value > 180) {
                usage(argv[0]);
                return 2;
            }
            angles[i] = (int)value;
        }
        return servo_run_legs(angles, upper);
    }
    if (argc == 4 && (strcmp(argv[1], "servo") == 0 || strcmp(argv[1], "leg") == 0)) {
        servo_id_t id;
        char *end;
        if (parse_bottom_servo(argv[2], &id) < 0) { usage(argv[0]); return 2; }
        errno = 0;
        long angle = strtol(argv[3], &end, 10);
        if (errno || end == argv[3] || *end != '\0' || angle < 0 || angle > 180) {
            usage(argv[0]);
            return 2;
        }
        return servo_run_bottom(0, strcmp(argv[1], "leg") == 0, id, (int)angle, stopping);
    }
    if (argc == 3 && strcmp(argv[1], "servo") == 0) {
        char *end;
        errno = 0;
        long angle = strtol(argv[2], &end, 10);
        if (errno || end == argv[2] || *end != '\0' || angle < 0 || angle > 180) {
            usage(argv[0]);
            return 2;
        }
        return servo_run_single(0, (int)angle, stopping);
    }
    if (argc == 3 && strcmp(argv[1], "face") == 0 && strcmp(argv[2], "list") == 0) {
        for (int i = 0; i < FACE_COUNT; ++i) puts(face_name((face_id_t)i));
        return 0;
    }
    face_id_t selected = FACE_NEUTRAL;
    if (argc != 1 && (argc < 3 || argc > 4 || strcmp(argv[1], "face") != 0 ||
                      parse_face(argv[2], &selected) < 0)) {
        usage(argv[0]);
        return 2;
    }
    const char *path = argc == 4 ? argv[3] : "/dev/i2c-3";
    oled_t oled = {.fd = -1};
    face_t face = {0};
    int result = 1;

    /* Face mode initializes only the OLED, never PCA9685 or movement. */
    if (oled_init(&oled, path, OLED_DEFAULT_ADDRESS) < 0) {
        perror("OLED init"); goto done;
    }
    if (face_init(&face, &oled) < 0 || face_set(&face, selected) < 0) {
        perror("face display"); goto done;
    }
    if (argc == 1) puts("OLED-only test. Enter a face name or quit.");
    char line[64];
    while (!*stopping) {
        if (face_update(&face) < 0) { perror("face update"); goto done; }
        struct pollfd input = {.fd = 0, .events = POLLIN};
        /* 40ms polling bounds animation latency without blocking other work. */
        int ready = poll(argc == 1 ? &input : NULL, argc == 1 ? 1U : 0U, 40);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll"); goto done;
        }
        if (argc == 1 && ready > 0) {
            if (input.revents & (POLLERR | POLLNVAL)) { errno = EIO; perror("stdin"); goto done; }
            if (input.revents & (POLLIN | POLLHUP)) {
                if (!fgets(line, sizeof(line), stdin)) {
                    if (ferror(stdin)) { perror("stdin"); goto done; }
                    break;
                }
                line[strcspn(line, "\r\n")] = '\0';
                if (strcmp(line, "quit") == 0) break;
                if (strcmp(line, "list") == 0) {
                    for (int i = 0; i < FACE_COUNT; ++i) puts(face_name((face_id_t)i));
                    continue;
                }
                if (parse_face(line, &selected) < 0) {
                    puts("Unknown face; type list to see available names.");
                    continue;
                }
                if (face_set(&face, selected) < 0) { perror("face display"); goto done; }
            }
        }
    }
    result = 0;
done:
    face_close(&face);
    if (oled_close(&oled) < 0) { perror("OLED close"); result = 1; }
    return result;
}
