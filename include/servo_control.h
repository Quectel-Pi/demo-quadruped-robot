#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "pca9685.h"
#include <signal.h>
#include <stdint.h>

/* Servo supplier's nominal 0..180 degree pulse range.
 * Mechanical end stops and installed joint limits still require verification. */
#define SERVO_MIN_US 500U
#define SERVO_MAX_US 2500U

typedef enum {
    SERVO_L1 = 0, SERVO_L2 = 1, SERVO_R1 = 2, SERVO_R2 = 3,
    SERVO_L3 = 4, SERVO_L4 = 5, SERVO_R3 = 6, SERVO_R4 = 7,
    SERVO_COUNT = 8
} servo_id_t;

typedef struct {
    int direction; /* +1 normal or -1 reversed, determined by measurement. */
    uint16_t min_us, max_us;
    int subtrim_degrees;
    int calibrated;
} servo_calibration_t;

typedef struct {
    pca9685_t *driver;
    servo_calibration_t calibration[SERVO_COUNT];
} servo_control_t;

/* Does not arm or move any servo. Logical IDs map to PCA9685 CH0..CH7. */
int servo_control_init(servo_control_t *control, pca9685_t *driver);
/* Explicit calibration required before servo_set_angle will write PWM. */
int servo_set_calibration(servo_control_t *control, servo_id_t id,
                          int direction, uint16_t min_us, uint16_t max_us);
int servo_set_subtrim(servo_control_t *control, servo_id_t id, int degrees);
/* Raw angle: applies calibration direction and subtrim once. */
int servo_set_angle(servo_control_t *control, servo_id_t id, int target_degrees);
/* Logical angle: CH0..CH7 accept 0..180.
 * L1/L2/L3/R4 mirror (180 - logical); R1/R2/L4/R3 use logical directly.
 * Requires direction=1 to prevent calibration from mirroring twice;
 * servo_set_angle applies subtrim exactly once after this conversion. */
int servo_set_leg_angle(servo_control_t *control, servo_id_t id, int logical_degrees);
/* Validate all four logical angles before writing CH0..CH3 in order. */
int servo_set_legs_angles(servo_control_t *control, int l1, int l2, int r1, int r2);
int servo_all_disable(servo_control_t *control);

/* CH0 bench test only: supplier's nominal 500..2500us mapping.
 * Keep all other servo channels physically disconnected. */
typedef struct {
    pca9685_t driver;
    servo_control_t control;
} servo_test_t;
int servo_test_init(servo_test_t *test);
int servo_test_set_angle(servo_test_t *test, int angle_degrees);
int servo_test_close(servo_test_t *test);

/* L1/CH0, L2/CH1, R1/CH2, R2/CH3, L3/CH4, L4/CH5, R3/CH6, R4/CH7.
 * Initialization does not command any angle; direction=1, subtrim=0.
 * Center/legs commands still change only CH0..CH3; close leaves PWM running. */
typedef struct {
    pca9685_t driver;
    servo_control_t control;
} servo_four_t;
int servo_four_init(servo_four_t *servos);
int servo_four_set_angle(servo_four_t *servos, servo_id_t id, int angle_degrees);
int servo_four_close(servo_four_t *servos);

/* CLI servo runners; stop flag is owned by the entry point. */
const char *servo_bottom_name(servo_id_t id);
/* upper=0: CH0..CH3 (0..180); upper=1: CH4..CH7 (0..180). */
int servo_run_legs(const int angles[4], int upper);
int servo_run_bottom(int center_all, int logical, servo_id_t selected, int angle,
                     const volatile sig_atomic_t *stopping);
int servo_run_single(int automatic, int angle, const volatile sig_atomic_t *stopping);

#endif