/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_TILT_H_
#define PHIAL_TILT_H_

#include <stdbool.h>

/* Returned by tilt_sector / tilt_update when the board is within the level
 * deadzone. Otherwise the return is a compass sector 0..11 (0 = North,
 * increasing clockwise). */
#define TILT_LEVEL (-1)

/* Calibration. Defaults assume the chip's +X = board East, +Y = board North,
 * +Z = up. Real mounting rotation/mirroring is dialed in on the bench, then
 * baked into TILT_CAL_DEFAULT. Thresholds are in m/s^2 of in-plane gravity
 * (|g| ~ 9.8; enter ~2.5 ~= 15deg tilt, exit ~1.5 ~= 9deg). */
struct tilt_cal {
    float offset_deg;       /* added to the compass angle (chip-vs-ring rotation) */
    bool  swap_xy;          /* swap axes if the chip is mounted rotated 90deg */
    bool  invert_x;         /* per-axis sign flips for mirror/rotation */
    bool  invert_y;
    float enter_ms2;        /* in-plane |g| to leave level (show a ring LED) */
    float exit_ms2;         /* in-plane |g| to return to level (< enter = hysteresis) */
    float seam_margin_deg;  /* extra angle past a seam before the lit sector changes */
};

/* offset_deg = 270 (= -90): bench-calibrated chip-vs-ring rotation. With the
 * 12-o'clock (North) edge pointed down, the chip reads ax~=-g (its +X points
 * to ring-West); +270deg rotates the compass so downhill lights the correct LED.
 * Verified on hardware across all four cardinals. */
#define TILT_CAL_DEFAULT {            \
    .offset_deg = 270.0f,             \
    .swap_xy = false,                 \
    .invert_x = false,                \
    .invert_y = false,                \
    .enter_ms2 = 2.5f,                \
    .exit_ms2 = 1.5f,                 \
    .seam_margin_deg = 6.0f,          \
}

struct tilt_state {
    struct tilt_cal cal;
    int last;   /* last reported target: TILT_LEVEL or 0..11 */
};

/* Stateless: TILT_LEVEL if in-plane |g| < cal->enter_ms2, else compass sector
 * 0..11 (downhill direction). */
int tilt_sector(float ax, float ay, const struct tilt_cal *cal);

/* Stateful: like tilt_sector but adds enter/exit deadzone hysteresis and a
 * seam margin, both keyed off st->last, to prevent flicker. Updates st->last. */
int tilt_update(struct tilt_state *st, float ax, float ay);

#endif /* PHIAL_TILT_H_ */
