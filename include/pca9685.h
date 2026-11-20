#ifndef PCA9685_H
#define PCA9685_H

#include <stdint.h>

#define PCA9685_DEFAULT_ADDRESS 0x40U /* 7-bit address; check board jumpers. */
#define PCA9685_DEFAULT_FREQ_HZ 50.0

typedef struct {
    int fd;
    double frequency_hz; /* Actual frequency implied by prescale, 25 MHz nominal oscillator. */
} pca9685_t;

/* No bus number is assumed: pass an explicit /dev/i2c-* path.
 * Reuse a running 50Hz configuration without writing PWM/MODE registers;
 * only reconfigure when every channel is inactive. */
int pca9685_init(pca9685_t *device, const char *path, uint8_t address);
int pca9685_set_pwm_freq(pca9685_t *device, double frequency_hz);
/* 0..4095 only. Use pca9685_disable_channel() to turn an output fully off. */
int pca9685_set_pwm(pca9685_t *device, unsigned channel, uint16_t on, uint16_t off);
int pca9685_disable_channel(pca9685_t *device, unsigned channel);
/* Close only the file descriptor; leave the last PWM output unchanged. */
int pca9685_close(pca9685_t *device);

/* PCA9685 VCC powers its logic; V+ powers the servos. Use a suitably rated
 * separate servo supply and connect its ground to PCA9685/M2 ground. Verify
 * SDA/SCL pull-up voltage is safe for the M2 before wiring or powering up. */

#endif