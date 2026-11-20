#define _POSIX_C_SOURCE 200809L
#include "pca9685.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MODE1 0x00U
#define MODE2 0x01U
#define MODE1_SLEEP 0x10U
#define MODE1_AI 0x20U
#define MODE1_EXTCLK 0x40U
#define MODE1_RESTART 0x80U
#define PRESCALE 0xFEU
#define LED0_ON_L 0x06U
#define ALL_LED_ON_H 0xFBU
#define ALL_LED_OFF_H 0xFDU
#define OSCILLATOR_HZ 25000000.0 /* Nominal internal oscillator; calibrate if needed. */

static int send_bytes(int fd, const uint8_t *bytes, size_t count)
{
    ssize_t sent = write(fd, bytes, count);
    if (sent == (ssize_t)count) return 0;
    if (sent >= 0) errno = EIO;
    return -1;
}

static int write_register(pca9685_t *device, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[2] = {reg, value};
    return send_bytes(device->fd, bytes, sizeof(bytes));
}

static int read_register(pca9685_t *device, uint8_t reg, uint8_t *value)
{
    if (send_bytes(device->fd, &reg, 1) < 0) return -1;
    ssize_t received = read(device->fd, value, 1);
    if (received == 1) return 0;
    if (received >= 0) errno = EIO;
    return -1;
}

static int check_channels_inactive(pca9685_t *device)
{
    /* Global FULL_ON/FULL_OFF can mask per-channel state: never change the
     * shared frequency while either override is set. */
    uint8_t global_on, global_off;
    if (read_register(device, ALL_LED_ON_H, &global_on) < 0 ||
        read_register(device, ALL_LED_OFF_H, &global_off) < 0) return -1;
    if ((global_on | global_off) & 0x10U) { errno = EBUSY; return -1; }
    for (unsigned ch = 0; ch < 16; ++ch) {
        uint8_t values[4];
        for (unsigned i = 0; i < 4; ++i)
            if (read_register(device, (uint8_t)(LED0_ON_L + 4U * ch + i),
                              &values[i]) < 0) return -1;
        unsigned on = (unsigned)values[0] | ((unsigned)(values[1] & 0x0fU) << 8);
        unsigned off = (unsigned)values[2] | ((unsigned)(values[3] & 0x0fU) << 8);
        if (!(values[3] & 0x10U) && ((values[1] & 0x10U) || on != off)) {
            errno = EBUSY;
            return -1;
        }
    }
    return 0;
}

int pca9685_set_pwm_freq(pca9685_t *device, double frequency_hz)
{
    if (!device || device->fd < 0 || !isfinite(frequency_hz) || frequency_hz <= 0) {
        errno = EINVAL;
        return -1;
    }
    double prescale = round(OSCILLATOR_HZ / (4096.0 * frequency_hz)) - 1.0;
    if (prescale < 3.0 || prescale > 255.0) {
        errno = ERANGE;
        return -1;
    }
    uint8_t mode;
    if (read_register(device, MODE1, &mode) < 0) return -1;
    if (mode & MODE1_EXTCLK) { errno = EBUSY; return -1; }
    if (check_channels_inactive(device) < 0) return -1;
    /* RESTART must not be asserted in the SLEEP or wake-up writes. Preserve
     * all other bits, enabling AI for the four-byte LED0 block transaction. */
    uint8_t awake = (uint8_t)((mode | MODE1_AI) &
                              (uint8_t)~(MODE1_SLEEP | MODE1_RESTART));
    if (write_register(device, MODE1, (uint8_t)(awake | MODE1_SLEEP)) < 0 ||
        write_register(device, PRESCALE, (uint8_t)prescale) < 0 ||
        write_register(device, MODE1, awake) < 0) return -1;
    const struct timespec settle = {0, 5000000L};
    if (nanosleep(&settle, NULL) < 0) return -1;
    if (write_register(device, MODE1, (uint8_t)(awake | MODE1_RESTART)) < 0) return -1;
    device->frequency_hz = OSCILLATOR_HZ / (4096.0 * (prescale + 1.0));
    return 0;
}

static int init_channels(pca9685_t *device, const char *path, uint8_t address)
{
    if (!device || !path || address < 0x08U || address > 0x77U) {
        errno = EINVAL;
        return -1;
    }
    device->fd = -1;
    device->frequency_hz = 0;
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    device->fd = fd;
    if (ioctl(fd, I2C_SLAVE, address) < 0) goto fail;
    uint8_t mode1, mode2, prescale, global_on, global_off;
    if (read_register(device, MODE2, &mode2) < 0 ||
        read_register(device, MODE1, &mode1) < 0 ||
        read_register(device, PRESCALE, &prescale) < 0 ||
        read_register(device, ALL_LED_ON_H, &global_on) < 0 ||
        read_register(device, ALL_LED_OFF_H, &global_off) < 0) goto fail;
    (void)mode2; /* Preserve existing output-driver configuration. */
    if ((global_on | global_off) & 0x10U || (mode1 & MODE1_EXTCLK)) {
        /* Global overrides mask outputs; external clock invalidates 25MHz math. */
        errno = EBUSY;
        goto fail;
    }
    uint8_t target = (uint8_t)(round(OSCILLATOR_HZ /
                             (4096.0 * PCA9685_DEFAULT_FREQ_HZ)) - 1.0);
    /* RESTART may remain set on a running chip; do not write MODE1 just
     * because of that bit. AI is required by the 4-byte PWM block write. */
    if (prescale == target && !(mode1 & MODE1_SLEEP) && (mode1 & MODE1_AI)) {
        device->frequency_hz = OSCILLATOR_HZ / (4096.0 * (prescale + 1.0));
        return 0;
    }
    if (pca9685_set_pwm_freq(device, PCA9685_DEFAULT_FREQ_HZ) < 0) goto fail;
    return 0;
fail:
    {
        int saved = errno;
        close(fd);
        device->fd = -1;
        errno = saved;
    }
    return -1;
}

int pca9685_init(pca9685_t *device, const char *path, uint8_t address)
{
    return init_channels(device, path, address);
}

int pca9685_set_pwm(pca9685_t *device, unsigned channel, uint16_t on, uint16_t off)
{
    if (!device || device->fd < 0 || channel >= 16 || on > 4095 || off > 4095) {
        errno = EINVAL;
        return -1;
    }
    const uint8_t bytes[] = {
        (uint8_t)(LED0_ON_L + 4U * channel),
        (uint8_t)(on & 0xFFU), (uint8_t)(on >> 8),
        (uint8_t)(off & 0xFFU), (uint8_t)(off >> 8)
    };
    return send_bytes(device->fd, bytes, sizeof(bytes));
}

int pca9685_disable_channel(pca9685_t *device, unsigned channel)
{
    if (!device || device->fd < 0 || channel >= 16) {
        errno = EINVAL;
        return -1;
    }
    /* FULL_OFF is bit 4 of LEDn_OFF_H; zero all other channel registers. */
    const uint8_t bytes[] = {(uint8_t)(LED0_ON_L + 4U * channel), 0, 0, 0, 0x10U};
    return send_bytes(device->fd, bytes, sizeof(bytes));
}

int pca9685_close(pca9685_t *device)
{
    if (!device || device->fd < 0) return 0;
    int result = close(device->fd);
    device->fd = -1;
    device->frequency_hz = 0;
    return result;
}