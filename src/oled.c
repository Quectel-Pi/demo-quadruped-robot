#include "oled.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int send_bytes(int fd, const uint8_t *bytes, size_t length)
{
    ssize_t result = write(fd, bytes, length);
    if (result == (ssize_t)length) return 0;
    if (result >= 0) errno = EIO;
    return -1;
}

static int send_command(oled_t *oled, uint8_t command)
{
    const uint8_t packet[2] = {0x00U, command}; /* D/C#=0 */
    return send_bytes(oled->fd, packet, sizeof(packet));
}

int oled_init(oled_t *oled, const char *path, uint8_t address)
{
    if (!oled || !path || address < 0x08U || address > 0x77U) {
        errno = EINVAL; return -1;
    }
    oled->fd = -1;
    memset(oled->framebuffer, 0, sizeof(oled->framebuffer));
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    oled->fd = fd;
    if (ioctl(fd, I2C_SLAVE, address) < 0) goto fail;
    /* Mirrors STM32 OLED_Init: 128x64, charge pump, page addressing mode. */
    static const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x30, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_commands); ++i)
        if (send_command(oled, init_commands[i]) < 0) goto fail;
    return 0;
fail:
    {
        int saved = errno;
        close(fd);
        oled->fd = -1;
        errno = saved;
    }
    return -1;
}

void oled_clear(oled_t *oled)
{
    if (oled) memset(oled->framebuffer, 0, sizeof(oled->framebuffer));
}

int oled_draw_bitmap(oled_t *oled, int x, int y, const uint8_t *bitmap,
                     size_t length, unsigned width, unsigned height)
{
    if (!oled || !bitmap || width == 0 || height == 0 ||
        width > OLED_WIDTH || height > OLED_HEIGHT ||
        length < ((width + 7U) / 8U) * height) {
        errno = EINVAL; return -1;
    }
    size_t stride = (width + 7U) / 8U;
    for (unsigned row = 0; row < height; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            int px = x + (int)col, py = y + (int)row;
            if (px < 0 || px >= (int)OLED_WIDTH || py < 0 || py >= (int)OLED_HEIGHT) continue;
            if (bitmap[row * stride + col / 8U] & (uint8_t)(0x80U >> (col % 8U)))
                oled->framebuffer[(unsigned)px + ((unsigned)py / 8U) * OLED_WIDTH] |=
                    (uint8_t)(1U << ((unsigned)py % 8U));
        }
    }
    return 0;
}

int oled_display(oled_t *oled)
{
    if (!oled || oled->fd < 0) { errno = EINVAL; return -1; }
    /* Page addressing from STM32 source: page B0..B7, column low/high 00/10.
     * 16-byte chunks avoid assuming adapter I2C transfer-size limits. */
    for (unsigned page = 0; page < 8; ++page) {
        if (send_command(oled, (uint8_t)(0xB0U | page)) < 0 ||
            send_command(oled, 0x00U) < 0 || send_command(oled, 0x10U) < 0) return -1;
        for (unsigned col = 0; col < OLED_WIDTH; col += 16) {
            uint8_t packet[17] = {0x40U}; /* D/C#=1: data bytes follow. */
            memcpy(packet + 1, &oled->framebuffer[page * OLED_WIDTH + col], 16);
            if (send_bytes(oled->fd, packet, sizeof(packet)) < 0) return -1;
        }
    }
    return 0;
}

int oled_close(oled_t *oled)
{
    if (!oled || oled->fd < 0) return 0;
    int result = close(oled->fd);
    oled->fd = -1;
    return result;
}