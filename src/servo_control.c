#include "servo_control.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#endif

int servo_control_init(servo_control_t *control, pca9685_t *driver)
{
    if (!control || !driver || driver->fd < 0 || driver->frequency_hz <= 0) {
        errno = EINVAL;
        return -1;
    }
    control->driver = driver;
    for (unsigned i = 0; i < SERVO_COUNT; ++i) {
        control->calibration[i] = (servo_calibration_t){0};
    }
    return 0;
}

int servo_set_calibration(servo_control_t *control, servo_id_t id,
                          int direction, uint16_t min_us, uint16_t max_us)
{
    if (!control || (unsigned)id >= SERVO_COUNT || (direction != 1 && direction != -1) ||
        min_us == 0 || min_us >= max_us || max_us >= 20000U) {
        errno = EINVAL;
        return -1;
    }
    servo_calibration_t *cal = &control->calibration[id];
    cal->direction = direction;
    cal->min_us = min_us;
    cal->max_us = max_us;
    cal->calibrated = 1;
    return 0;
}

int servo_set_subtrim(servo_control_t *control, servo_id_t id, int degrees)
{
    if (!control || (unsigned)id >= SERVO_COUNT || degrees < -90 || degrees > 90) {
        errno = EINVAL;
        return -1;
    }
    control->calibration[id].subtrim_degrees = degrees;
    return 0;
}

int servo_set_angle(servo_control_t *control, servo_id_t id, int target_degrees)
{
    if (!control || !control->driver || (unsigned)id >= SERVO_COUNT ||
        target_degrees < 0 || target_degrees > 180) {
        errno = EINVAL;
        return -1;
    }
    servo_calibration_t *cal = &control->calibration[id];
    if (!cal->calibrated || control->driver->frequency_hz <= 0) {
        errno = EPERM; /* Fail closed until per-channel calibration is explicit. */
        return -1;
    }
    int angle = cal->direction == 1 ? target_degrees : 180 - target_degrees;
    angle += cal->subtrim_degrees;
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    double pulse_us = cal->min_us + (cal->max_us - cal->min_us) * (angle / 180.0);
    long count = lround(pulse_us * control->driver->frequency_hz * 4096.0 / 1000000.0);
    if (count < 1 || count > 4095) {
        errno = ERANGE;
        return -1;
    }
    return pca9685_set_pwm(control->driver, (unsigned)id, 0, (uint16_t)count);
}

int servo_set_leg_angle(servo_control_t *control, servo_id_t id, int logical_degrees)
{
    if (!control || (unsigned)id >= SERVO_COUNT ||
        logical_degrees < 0 || logical_degrees > 180) {
        errno = EINVAL;
        return -1;
    }
    /* A -1 calibration direction would undo the left/right mirror. */
    if (control->calibration[id].direction != 1) {
        errno = EPERM;
        return -1;
    }
    int physical_degrees = id == SERVO_L1 || id == SERVO_L2 ||
                           id == SERVO_L3 || id == SERVO_R4
                               ? 180 - logical_degrees : logical_degrees;
    return servo_set_angle(control, id, physical_degrees);
}

int servo_set_legs_angles(servo_control_t *control, int l1, int l2, int r1, int r2)
{
    const int angles[] = {l1, l2, r1, r2};
    if (!control) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < 4; ++i) {
        if (angles[i] < 0 || angles[i] > 180) { errno = EINVAL; return -1; }
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (servo_set_leg_angle(control, (servo_id_t)i, angles[i]) < 0) return -1;
    }
    return 0;
}

int servo_all_disable(servo_control_t *control)
{
    if (!control || !control->driver) { errno = EINVAL; return -1; }
    int result = 0;
    for (unsigned i = 0; i < SERVO_COUNT; ++i)
        if (pca9685_disable_channel(control->driver, i) < 0) result = -1;
    return result;
}

int servo_test_init(servo_test_t *test)
{
    if (!test) { errno = EINVAL; return -1; }
    test->driver.fd = -1;
    test->control.driver = NULL;
    if (pca9685_init(&test->driver, "/dev/i2c-7", PCA9685_DEFAULT_ADDRESS) < 0)
        return -1;
    if (servo_control_init(&test->control, &test->driver) < 0 ||
        servo_set_calibration(&test->control, SERVO_L1, 1,
                              SERVO_MIN_US, SERVO_MAX_US) < 0) {
        int saved = errno;
        (void)pca9685_close(&test->driver);
        errno = saved;
        return -1;
    }
    return 0;
}

int servo_test_set_angle(servo_test_t *test, int angle_degrees)
{
    if (!test || angle_degrees < 0 || angle_degrees > 180) {
        errno = EINVAL; return -1;
    }
    return servo_set_angle(&test->control, SERVO_L1, angle_degrees);
}

int servo_test_close(servo_test_t *test)
{
    if (!test) { errno = EINVAL; return -1; }
    test->control.driver = NULL;
    return pca9685_close(&test->driver);
}

int servo_four_init(servo_four_t *servos)
{
    if (!servos) { errno = EINVAL; return -1; }
    servos->driver.fd = -1;
    servos->control.driver = NULL;
    if (pca9685_init(&servos->driver, "/dev/i2c-7", PCA9685_DEFAULT_ADDRESS) < 0)
        return -1;
    if (servo_control_init(&servos->control, &servos->driver) < 0)
        goto fail;
    for (unsigned ch = SERVO_L1; ch < SERVO_COUNT; ++ch) {
        if (servo_set_calibration(&servos->control, (servo_id_t)ch, 1,
                                  SERVO_MIN_US, SERVO_MAX_US) < 0)
            goto fail;
    }
    return 0;
fail:
    {
        int saved = errno;
        (void)pca9685_close(&servos->driver);
        servos->control.driver = NULL;
        errno = saved;
    }
    return -1;
}

int servo_four_set_angle(servo_four_t *servos, servo_id_t id, int angle_degrees)
{
    if (!servos || (unsigned)id >= SERVO_COUNT || angle_degrees < 0 || angle_degrees > 180) {
        errno = EINVAL;
        return -1;
    }
    return servo_set_angle(&servos->control, id, angle_degrees);
}

int servo_four_close(servo_four_t *servos)
{
    if (!servos) { errno = EINVAL; return -1; }
    servos->control.driver = NULL;
    return pca9685_close(&servos->driver);
}

const char *servo_bottom_name(servo_id_t id)
{
    static const char *const names[] = {"L1", "L2", "R1", "R2", "L3", "L4", "R3", "R4"};
    return (unsigned)id < SERVO_COUNT ? names[id] : NULL;
}

int servo_run_legs(const int angles[4], int upper)
{
    if (!angles) { errno = EINVAL; return 1; }
    for (unsigned i = 0; i < 4; ++i) {
        if (angles[i] < 0 || angles[i] > 180) {
            errno = EINVAL;
            return 1;
        }
    }
    servo_four_t servos = {.driver = {.fd = -1}};
    if (servo_four_init(&servos) < 0) {
        perror("PCA9685 CH0-CH3 initialization (/dev/i2c-7, 0x40)");
        return 1;
    }
    int result = 0;
    if (upper) {
        for (unsigned i = 0; i < 4; ++i) {
            if (servo_set_leg_angle(&servos.control, (servo_id_t)(SERVO_L3 + i), angles[i]) < 0) {
                result = 1;
                break;
            }
        }
    } else if (servo_set_legs_angles(&servos.control, angles[0], angles[1], angles[2], angles[3]) < 0) {
        result = 1;
    }
    if (result) {
        perror("PCA9685 legs angle/PWM write");
    }
    if (servo_four_close(&servos) < 0) {
        perror("PCA9685 close");
        result = 1;
    }
    return result;
}

int servo_run_bottom(int center_all, int logical, servo_id_t selected, int angle,
                     const volatile sig_atomic_t *stopping)
{
    servo_four_t servos = {.driver = {.fd = -1}};
    int result = 1;
    if (servo_four_init(&servos) < 0) {
        perror("PCA9685 CH0-CH3 initialization (/dev/i2c-7, 0x40)");
        return 1;
    }
    for (unsigned ch = 0; ch < (center_all ? 4U : 1U) && !*stopping; ++ch) {
        servo_id_t id = center_all ? (servo_id_t)ch : selected;
        int target = center_all ? 90 : angle;
        if ((logical ? servo_set_leg_angle(&servos.control, id, target) :
                       servo_four_set_angle(&servos, id, target)) < 0) {
            perror("PCA9685 servo angle/PWM write");
            goto done;
        }
        printf("CH%u / %s -> %d degrees%s\n", (unsigned)id, servo_bottom_name(id), target,
               logical ? " (logical)" : " (physical)");
        fflush(stdout);
        /* Stagger start-up current; an interrupt stops before the next write. */
        if (center_all && ch < 3 && !*stopping) {
#ifdef _WIN32
            Sleep(500);
#else
            if (poll(NULL, 0, 500) < 0 && errno != EINTR) {
                perror("servo-center delay");
                goto done;
            }
#endif
        }
    }
    result = 0;
done:
    if (servo_four_close(&servos) < 0) {
        perror("PCA9685 close");
        result = 1;
    }
    return result;
}

int servo_run_single(int automatic, int angle, const volatile sig_atomic_t *stopping)
{
    servo_test_t test = {.driver = {.fd = -1}};
    int result = 1;
    if (servo_test_init(&test) < 0) {
        perror("PCA9685 CH0 initialization (/dev/i2c-7, 0x40)");
        return 1;
    }
    const int sequence[] = {90, 45, 0, 45, 90, 135, 180, 135, 90};
    size_t steps = automatic ? sizeof(sequence) / sizeof(sequence[0]) : 1U;
    for (size_t i = 0; i < steps && !*stopping; ++i) {
        int target = automatic ? sequence[i] : angle;
        if (servo_test_set_angle(&test, target) < 0) {
            perror("PCA9685 CH0 angle/PWM write");
            goto done;
        }
        printf("CH0: %d degrees\n", target);
        fflush(stdout);
        if (automatic && i + 1U < steps && !*stopping) (void)sleep(1);
    }
    result = 0;
done:
    if (servo_test_close(&test) < 0) {
        perror("PCA9685 close");
        result = 1;
    }
    return result;
}