#ifndef OLED_H
#define OLED_H

#include <stddef.h>
#include <stdint.h>

#define OLED_WIDTH 128U
#define OLED_HEIGHT 64U
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_HEIGHT / 8U)
/* balance_car_stm32/Core/Src/oled.c defines OLED_ADDRESS_7BIT=0x3C,
 * OLED_ADDRESS_HAL=(0x3C << 1)=0x78 for HAL_I2C_Master_Transmit(hi2c1).
 * Linux I2C_SLAVE takes the unshifted 7-bit slave address 0x3C, NOT 0x78. */
#define OLED_DEFAULT_ADDRESS 0x3CU

typedef struct {
    int fd;
    uint8_t framebuffer[OLED_BUFFER_SIZE]; /* SSD1306 page order, bit0 = top row. */
} oled_t;

int oled_init(oled_t *oled, const char *path, uint8_t address);
void oled_clear(oled_t *oled); /* Clears RAM framebuffer; call oled_display to flush. */
int oled_display(oled_t *oled);
/* Source bitmap uses image2cpp/Adafruit horizontal MSB-first bytes, not page order.
 * Source length must be >= ((width+7)/8)*height; clipping is supported. */
int oled_draw_bitmap(oled_t *oled, int x, int y, const uint8_t *bitmap,
                     size_t length, unsigned width, unsigned height);
int oled_close(oled_t *oled);

#endif