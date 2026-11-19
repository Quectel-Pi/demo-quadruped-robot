#include "face_bitmaps.h"

#include <string.h>

/* Original geometry on a 128x64 canvas: eye centers (38,32)/(90,32),
 * brow band y=8..23, mouth band y=45..55. No external image assets. */
static void pixel(uint8_t *image, int x, int y, int on)
{
    if (x < 0 || x >= (int)OLED_WIDTH || y < 0 || y >= (int)OLED_HEIGHT) return;
    uint8_t mask = (uint8_t)(0x80U >> (x & 7));
    uint8_t *cell = &image[y * (int)(OLED_WIDTH / 8U) + x / 8];
    if (on) *cell |= mask;
    else *cell &= (uint8_t)~mask;
}

static void ellipse(uint8_t *image, int cx, int cy, int rx, int ry, int on)
{
    if (rx < 1 || ry < 1) return;
    for (int y = cy - ry; y <= cy + ry; ++y) {
        for (int x = cx - rx; x <= cx + rx; ++x) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
                pixel(image, x, y, on);
        }
    }
}

static void line(uint8_t *image, int x0, int y0, int x1, int y1, int radius)
{
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int steps = ax > ay ? ax : ay;
    for (int i = 0; i <= steps; ++i) {
        int x = steps ? x0 + dx * i / steps : x0;
        int y = steps ? y0 + dy * i / steps : y0;
        ellipse(image, x, y, radius, radius, 1);
    }
}

static void rounded_eye(uint8_t *image, int cx, int cy, int w, int h, int r)
{
    int left = cx - w / 2, top = cy - h / 2;
    for (int y = top; y < top + h; ++y) {
        for (int x = left; x < left + w; ++x) {
            int nx = x < left + r ? left + r :
                     (x >= left + w - r ? left + w - r - 1 : x);
            int ny = y < top + r ? top + r :
                     (y >= top + h - r ? top + h - r - 1 : y);
            int dx = x - nx, dy = y - ny;
            if (dx * dx + dy * dy <= r * r) pixel(image, x, y, 1);
        }
    }
}

static void rounded_pair(uint8_t *image, int w, int h)
{
    rounded_eye(image, 38, 32, w, h, 6);
    rounded_eye(image, 90, 32, w, h, 6);
}

static void small_round_mouth(uint8_t *image)
{
    ellipse(image, 64, 51, 4, 4, 1);
}

/* A flat upper lip and a rounded lower edge, without square side walls. */
static void happy_mouth(uint8_t *image)
{
    ellipse(image, 64, 43, 11, 10, 1);
    for (int y = 33; y < 40; ++y)
        for (int x = 53; x <= 75; ++x)
            pixel(image, x, y, 0);
}

static void sleepy_z(uint8_t *image, int x, int y, int size)
{
    line(image, x, y, x + size, y, 1);
    line(image, x + size, y, x, y + size, 1);
    line(image, x, y + size, x + size, y + size, 1);
}

/* An original four-rayed negative-space sparkle inside each white eye. */
static void star_cutout(uint8_t *image, int cx, int cy)
{
    for (int dy = -10; dy <= 10; ++dy) {
        for (int dx = -10; dx <= 10; ++dx) {
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if ((ax <= 2 && ay <= 10) || (ay <= 2 && ax <= 10) || ax + ay <= 8)
                pixel(image, cx + dx, cy + dy, 0);
        }
    }
}

void face_bitmaps_generate(face_bitmaps_t *bitmaps)
{
    if (!bitmaps) return;
    memset(bitmaps, 0, sizeof(*bitmaps));

    rounded_pair(bitmaps->images[EYES_NEUTRAL], 28, 26);
    rounded_pair(bitmaps->images[EYES_HALF_CLOSED], 28, 10);
    line(bitmaps->images[EYES_CLOSED], 25, 39, 51, 39, 1);
    line(bitmaps->images[EYES_CLOSED], 77, 39, 103, 39, 1);

    /* Happy: rounded arched eyes with a short, soft crest. */
    uint8_t *happy = bitmaps->images[EYES_HAPPY];
    for (int cx = 38; cx <= 90; cx += 52) {
        line(happy, cx - 14, 29, cx - 5, 19, 2);
        line(happy, cx - 5, 19, cx - 2, 17, 2);
        line(happy, cx - 2, 17, cx + 2, 17, 2);
        line(happy, cx + 2, 17, cx + 5, 19, 2);
        line(happy, cx + 5, 19, cx + 14, 29, 2);
    }
    happy_mouth(happy);

    /* Sad: inner brows lift, large upward-looking eyes and a tiny frown. */
    uint8_t *sad = bitmaps->images[EYES_SAD];
    rounded_pair(sad, 28, 26);
    line(sad, 23, 18, 38, 15, 1);
    line(sad, 38, 15, 52, 9, 1);
    line(sad, 76, 9, 90, 15, 1);
    line(sad, 90, 15, 105, 18, 1);
    ellipse(sad, 37, 26, 2, 2, 0);
    ellipse(sad, 89, 26, 2, 2, 0);
    line(sad, 58, 54, 63, 49, 1);
    line(sad, 63, 49, 66, 49, 1);
    line(sad, 66, 49, 71, 54, 1);

    /* Worried: each brow climbs from its outer end toward the nose;
     * the mirrored curves stay separate from the eyes below. */
    uint8_t *worried = bitmaps->images[EYES_WORRIED];
    rounded_pair(worried, 28, 26);
    line(worried, 23, 18, 30, 17, 1);
    line(worried, 30, 17, 39, 14, 1);
    line(worried, 39, 14, 48, 9, 1);
    line(worried, 80, 9, 89, 14, 1);
    line(worried, 89, 14, 98, 17, 1);
    line(worried, 98, 17, 105, 18, 1);

    /* Surprise: full round eyes with small pupils and a clear O-shaped mouth. */
    uint8_t *surprised = bitmaps->images[EYES_SURPRISED];
    for (int cx = 38; cx <= 90; cx += 52) {
        ellipse(surprised, cx, 31, 15, 15, 1);
        ellipse(surprised, cx, 31, 3, 4, 0);
    }
    small_round_mouth(surprised);

    /* Sleepy: broad, gently sagging closed lids, a small round mouth and Zs. */
    uint8_t *sleepy = bitmaps->images[EYES_SLEEPY];
    for (int cx = 38; cx <= 90; cx += 52) {
        for (int x = -15; x <= 15; ++x) {
            int top = 34 + (225 - x * x) / 56;
            int thickness = (x < -12 || x > 12) ? 3 : 4;
            for (int y = top; y < top + thickness; ++y)
                pixel(sleepy, cx + x, y, 1);
        }
    }
    ellipse(sleepy, 64, 51, 4, 4, 1);
    sleepy_z(sleepy, 98, 15, 5);
    sleepy_z(sleepy, 109, 6, 7);

    /* Angry: one-piece wedge-shaped eyes, with sloping upper edges rather
     * than eyebrows floating above rounded rectangles. */
    uint8_t *angry = bitmaps->images[EYES_ANGRY];
    rounded_eye(angry, 38, 34, 34, 25, 7);
    rounded_eye(angry, 90, 34, 34, 25, 7);
    for (int x = 21; x <= 54; ++x) {
        int top = 22 + (x - 21) * 10 / 33;
        for (int y = 20; y < top; ++y) pixel(angry, x, y, 0);
    }
    for (int x = 74; x <= 107; ++x) {
        int top = 22 + (107 - x) * 10 / 33;
        for (int y = 20; y < top; ++y) pixel(angry, x, y, 0);
    }
    /* A small, separate four-corner tension mark in the upper-right margin. */
    line(angry, 105, 7, 109, 10, 1);
    line(angry, 109, 10, 108, 12, 1);
    line(angry, 120, 7, 116, 10, 1);
    line(angry, 116, 10, 117, 12, 1);
    line(angry, 105, 19, 109, 16, 1);
    line(angry, 109, 16, 108, 14, 1);
    line(angry, 120, 19, 116, 16, 1);
    line(angry, 116, 16, 117, 14, 1);

    /* Awe: same two eye centers, large white disks with cut-out four-point
     * stars; the small mouth matches surprise but the eye language differs. */
    uint8_t *awe = bitmaps->images[EYES_AWE];
    for (int cx = 38; cx <= 90; cx += 52) {
        ellipse(awe, cx, 31, 16, 17, 1);
        star_cutout(awe, cx, 31);
        ellipse(awe, cx - 10, 21, 1, 1, 0);
    }
    small_round_mouth(awe);
}

static int image_pixel(const uint8_t *image, int x, int y)
{
    if (x < 0 || x >= (int)OLED_WIDTH || y < 0 || y >= (int)OLED_HEIGHT) return 0;
    return (image[y * (int)(OLED_WIDTH / 8U) + x / 8] & (0x80U >> (x & 7))) != 0;
}

static void copy_region(uint8_t *dst, const uint8_t *src,
                        int left, int top, int right, int bottom, int dx, int dy)
{
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            if (image_pixel(src, x, y)) pixel(dst, x + dx, y + dy, 1);
}

static void clear_region(uint8_t *dst, int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) pixel(dst, x, y, 0);
}

static void move_eyes(uint8_t *dst, const uint8_t *src, int dx, int dy, int angry)
{
    /* Only the eye bodies move: worried brows and the angry corner mark stay put. */
    int top = angry ? 21 : 19;
    for (int cx = 38; cx <= 90; cx += 52) {
        clear_region(dst, cx - 18, top, cx + 18, 48);
        copy_region(dst, src, cx - 18, top, cx + 18, 48,
                    angry && cx == 90 ? -dx : dx, dy);
    }
    if (!angry) copy_region(dst, src, 20, 0, 108, 18, 0, 0);
}

static void star_variant(uint8_t *dst, int cx, int cy, int reach)
{
    ellipse(dst, cx, cy, 16, 17, 1);
    for (int y = -reach; y <= reach; ++y)
        for (int x = -reach; x <= reach; ++x) {
            int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
            if ((ax <= 1 && ay <= reach) || (ay <= 1 && ax <= reach) ||
                ax + ay <= reach - 2)
                pixel(dst, cx + x, cy + y, 0);
        }
    ellipse(dst, cx - 9, cy - 9, 1, 1, 0);
}

void face_bitmap_render(const face_bitmaps_t *bitmaps, eyes_image_t image,
                        unsigned pose, uint8_t output[OLED_BUFFER_SIZE])
{
    if (!bitmaps || !output || image < EYES_NEUTRAL || image >= EYES_IMAGE_COUNT) return;
    const uint8_t *base = bitmaps->images[image];
    const uint8_t *neutral = bitmaps->images[EYES_NEUTRAL];
    memcpy(output, base, OLED_BUFFER_SIZE);
    if (pose == 0) return;

    switch (image) {
        case EYES_NEUTRAL:
            /* Infrequent glances move the pair together, not inward. */
            if (pose == 1 || pose == 2) {
                memset(output, 0, OLED_BUFFER_SIZE);
                copy_region(output, neutral, 20, 17, 108, 48, pose == 1 ? -2 : 2, 0);
            }
            break;
        case EYES_HAPPY:
            if (pose == 1) memcpy(output, neutral, OLED_BUFFER_SIZE);
            else if (pose == 2) clear_region(output, 50, 38, 78, 55);
            else if (pose == 4 || pose == 5) {
                memset(output, 0, OLED_BUFFER_SIZE);
                copy_region(output, base, 0, 0, 127, 63, 0, pose == 4 ? -2 : -1);
            }
            break;
        case EYES_SAD:
            if (pose == 1 || pose == 2) {
                memcpy(output, neutral, OLED_BUFFER_SIZE);
                copy_region(output, base, 20, 0, 108, 18, 0, 0);
                if (pose == 2) {
                    clear_region(output, 20, 19, 108, 47);
                    copy_region(output, base, 20, 19, 108, 47, 0, 1);
                }
            } else if (pose == 3 || pose == 5) move_eyes(output, base, 0, 1, 0);
            break;
        case EYES_WORRIED:
            if (pose == 1 || pose == 2) {
                memcpy(output, neutral, OLED_BUFFER_SIZE);
                copy_region(output, base, 20, 0, pose == 1 ? 55 : 108, 18, 0, 0);
            } else if (pose == 4 || pose == 5)
                move_eyes(output, base, pose == 4 ? -2 : 2, 0, 0);
            break;
        case EYES_SURPRISED:
            if (pose == 1) memcpy(output, neutral, OLED_BUFFER_SIZE);
            else if (pose == 2 || pose == 3) {
                clear_region(output, 20, 10, 55, 49);
                clear_region(output, 72, 10, 107, 49);
                for (int cx = 38; cx <= 90; cx += 52) {
                    ellipse(output, cx, 31, pose == 2 ? 13 : 17,
                            pose == 2 ? 13 : 18, 1);
                    ellipse(output, cx, 31, 3, 4, 0);
                }
                if (pose == 2) {
                    clear_region(output, 59, 46, 69, 56);
                    ellipse(output, 64, 51, 2, 2, 1);
                }
            }
            break;
        case EYES_SLEEPY:
            if (pose <= 3) {
                const uint8_t *src = pose == 1 ? neutral :
                    bitmaps->images[pose == 2 ? EYES_HALF_CLOSED : EYES_CLOSED];
                memcpy(output, src, OLED_BUFFER_SIZE);
            } else if (pose >= 4 && pose <= 7) {
                clear_region(output, 96, 4, 120, 25);
                if (pose == 4 || pose == 6) sleepy_z(output, 98, 15, 5);
                if (pose == 5 || pose == 6) sleepy_z(output, 109, 6, 7);
                if (pose == 7) sleepy_z(output, 106, 9, 6);
            }
            break;
        case EYES_ANGRY:
            if (pose == 1) memcpy(output, neutral, OLED_BUFFER_SIZE);
            else if (pose == 2) {
                /* Partial squeeze: sloping lids appear before the full glare. */
                memcpy(output, neutral, OLED_BUFFER_SIZE);
                copy_region(output, base, 20, 19, 108, 33, 0, 0);
            } else if (pose == 3 || pose == 5)
                move_eyes(output, base, pose == 3 ? 2 : 1, 0, 1);
            break;
        case EYES_AWE:
            if (pose == 1) memcpy(output, neutral, OLED_BUFFER_SIZE);
            else if (pose == 2) {
                memcpy(output, bitmaps->images[EYES_SURPRISED], OLED_BUFFER_SIZE);
                clear_region(output, 59, 46, 69, 56);
            } else if (pose == 3) clear_region(output, 59, 46, 69, 56);
            else if (pose == 5 || pose == 6) {
                clear_region(output, 20, 12, 56, 49);
                clear_region(output, 72, 12, 108, 49);
                star_variant(output, 38, 31, pose == 5 ? 7 : 9);
                star_variant(output, 90, 31, pose == 5 ? 9 : 7);
            }
            break;
        default: break;
    }
}