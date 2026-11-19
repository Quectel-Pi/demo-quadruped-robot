#ifndef FACE_BITMAPS_H
#define FACE_BITMAPS_H

#include "oled.h"

/* Original M2 geometry, generated as 128x64 horizontal MSB-first images.
 * Keep the verified oled_draw_bitmap() conversion to SSD1306 page order. */
typedef enum {
    EYES_NEUTRAL,
    EYES_HALF_CLOSED,
    EYES_CLOSED,
    EYES_HAPPY,
    EYES_SAD,
    EYES_WORRIED,
    EYES_SURPRISED,
    EYES_SLEEPY,
    EYES_ANGRY,
    EYES_AWE,
    EYES_IMAGE_COUNT
} eyes_image_t;

typedef struct {
    uint8_t images[EYES_IMAGE_COUNT][OLED_BUFFER_SIZE];
} face_bitmaps_t;

void face_bitmaps_generate(face_bitmaps_t *bitmaps);
/* Render one temporary 128x64 MSB-first image. pose=0 always means the
 * verified base face; other pose values are discrete animation keyframes. */
void face_bitmap_render(const face_bitmaps_t *bitmaps, eyes_image_t image,
                        unsigned pose, uint8_t output[OLED_BUFFER_SIZE]);

#endif