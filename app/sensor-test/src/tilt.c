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

/* Quantize a compass bearing to one of `n` equal sectors (0 = North, CW), with
 * seam hysteresis: when `last` is a valid sector, hold it until the bearing
 * moves more than (half-sector + seam_margin) from its center. */
static int quant_hyst(float compass, int n, int last, float seam_margin)
{
    float step = 360.0f / (float)n;
    int s = (((int)lroundf(compass / step)) % n + n) % n;

    if (last >= 0 && s != last) {
        float last_center = (float)last * step;
        float d = fmodf(fabsf(compass - last_center), 360.0f);
        if (d > 180.0f) d = 360.0f - d;
        if (d <= (step / 2.0f + seam_margin)) {
            s = last;
        }
    }
    return s;
}

/* Hysteretic "is m above boundary b": needs a wider push to cross when it would
 * change state. `currently_above` is the present side of the boundary. */
static bool above(float m, float b, float h, bool currently_above)
{
    return currently_above ? (m > b - h) : (m > b + h);
}

static enum tilt_mode classify(float m, enum tilt_mode last, const struct tilt_cal *c)
{
    bool above_level  = above(m, c->level_ms2,  c->zone_hyst_ms2, last != TILT_MODE_LEVEL);
    bool above_coarse = above(m, c->coarse_ms2, c->zone_hyst_ms2, last == TILT_MODE_COARSE);

    if (above_coarse) return TILT_MODE_COARSE;
    if (above_level)  return TILT_MODE_FINE;
    return TILT_MODE_LEVEL;
}

struct tilt_out tilt_update(struct tilt_state *st, float ax, float ay)
{
    const struct tilt_cal *c = &st->cal;
    float m = hypotf(ax, ay);
    enum tilt_mode mode = classify(m, st->last_mode, c);
    struct tilt_out out = { .mode = mode, .sector = -1 };

    if (mode != TILT_MODE_LEVEL) {
        float compass = downhill_compass_deg(c, ax, ay);
        if (mode == TILT_MODE_COARSE) {
            int last = (st->last_mode == TILT_MODE_COARSE) ? st->last_sector : -1;
            out.sector = quant_hyst(compass, 12, last, c->seam_margin_deg);
            st->last_sector = out.sector;
        } else { /* FINE */
            int last = (st->last_mode == TILT_MODE_FINE) ? st->last_quadrant : -1;
            out.sector = quant_hyst(compass, 4, last, c->seam_margin_deg);
            st->last_quadrant = out.sector;
        }
    }

    st->last_mode = mode;
    return out;
}
