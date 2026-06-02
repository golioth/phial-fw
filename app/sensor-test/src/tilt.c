/* SPDX-License-Identifier: Apache-2.0 */
#include "tilt.h"
#include <math.h>

/* M_PI is POSIX, not ISO C — Zephyr's strict-C11 libc may not define it. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RAD_TO_DEG (180.0f / (float)M_PI)

/* Apply axis swap/invert, return in *ox,*oy. */
static void apply_axes(const struct tilt_cal *cal, float ax, float ay,
                       float *ox, float *oy)
{
    if (cal->swap_xy) {
        float t = ax; ax = ay; ay = t;
    }
    if (cal->invert_x) ax = -ax;
    if (cal->invert_y) ay = -ay;
    *ox = ax;
    *oy = ay;
}

/* Downhill bearing as a compass angle in [0,360): 0=N, increasing clockwise. */
static float downhill_compass_deg(const struct tilt_cal *cal, float ax, float ay)
{
    float cx, cy;
    apply_axes(cal, ax, ay, &cx, &cy);

    /* Downhill direction = -(in-plane reaction vector) = (-cx, -cy). */
    float math_deg = atan2f(-cy, -cx) * RAD_TO_DEG;   /* CCW from +x (East) */
    float compass = 90.0f - math_deg + cal->offset_deg;
    compass = fmodf(compass, 360.0f);
    if (compass < 0.0f) compass += 360.0f;
    return compass;
}

int tilt_sector(float ax, float ay, const struct tilt_cal *cal)
{
    float mag = hypotf(ax, ay);
    if (mag < cal->enter_ms2) {
        return TILT_LEVEL;
    }
    float compass = downhill_compass_deg(cal, ax, ay);
    int sector = (int)lroundf(compass / 30.0f);
    return ((sector % 12) + 12) % 12;
}

int tilt_update(struct tilt_state *st, float ax, float ay)
{
    float mag = hypotf(ax, ay);
    const struct tilt_cal *cal = &st->cal;

    /* Deadzone with enter/exit hysteresis. */
    if (st->last == TILT_LEVEL) {
        if (mag < cal->enter_ms2) {
            return st->last;           /* stay level */
        }
    } else {
        if (mag < cal->exit_ms2) {
            st->last = TILT_LEVEL;     /* fall back to level */
            return st->last;
        }
    }

    float compass = downhill_compass_deg(cal, ax, ay);
    int sector = (((int)lroundf(compass / 30.0f)) % 12 + 12) % 12;

    /* Seam hysteresis: when already showing a ring LED, keep the current sector
     * until the angle moves more than (15 + seam_margin) deg from that sector's
     * center, so dithering at a seam doesn't blink between two LEDs. */
    if (st->last != TILT_LEVEL && sector != st->last) {
        float last_center = st->last * 30.0f;
        float d = fmodf(fabsf(compass - last_center), 360.0f);
        if (d > 180.0f) d = 360.0f - d;
        if (d <= (15.0f + cal->seam_margin_deg)) {
            sector = st->last;         /* within the sticky band, hold */
        }
    }

    st->last = sector;
    return sector;
}
