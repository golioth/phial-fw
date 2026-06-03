/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_TILT_H_
#define PHIAL_TILT_H_

#include <stdbool.h>

/* Returned by tilt_sector when the board is within the level deadzone.
 * Otherwise it returns a compass sector 0..11 (0 = North, clockwise). */
#define TILT_LEVEL (-1)

/* Display zones for the "zoom" level, selected by in-plane tilt magnitude:
 *   COARSE — large tilt: one of 12 outer ring positions (downhill bearing)
 *   FINE   — small tilt: one of 4 inner cardinals (downhill bearing)
 *   LEVEL  — near flat: caller lights all LEDs solid */
enum tilt_mode {
    TILT_MODE_LEVEL = 0,
    TILT_MODE_FINE,
    TILT_MODE_COARSE,
};

/* Result of tilt_update(). `sector` is board-agnostic: for COARSE it is an
 * outer position 0..11 (0=N, CW); for FINE a cardinal 0..3 (0=N,1=E,2=S,3=W);
 * for LEVEL it is -1 (unused). The caller maps sector -> physical LED. */
struct tilt_out {
    enum tilt_mode mode;
    int sector;
};

/* Calibration. Defaults assume the chip's +X = board East, +Y = board North,
 * +Z = up. Real mounting rotation/mirroring is dialed in on the bench, then
 * baked into TILT_CAL_DEFAULT. Thresholds are in m/s^2 of in-plane gravity
 * (|g| ~ 9.8; enter ~2.5 ~= 15deg tilt, exit ~1.5 ~= 9deg). */
struct tilt_cal {
    float offset_deg;       /* added to the compass angle (chip-vs-ring rotation) */
    bool  swap_xy;          /* swap axes if the chip is mounted rotated 90deg */
    bool  invert_x;         /* per-axis sign flips for mirror/rotation */
    bool  invert_y;
    float enter_ms2;        /* tilt_sector deadzone: in-plane |g| to leave level */
    float exit_ms2;         /* tilt_sector deadzone return (< enter = hysteresis) */
    float seam_margin_deg;  /* extra angle past a seam before the lit sector changes */

    /* Zone thresholds for tilt_update()'s zoom level (in-plane |g|, m/s^2): */
    float coarse_ms2;       /* >= this -> COARSE (outer ring) */
    float level_ms2;        /* <  this -> LEVEL (all LEDs); between -> FINE (inner) */
    float zone_hyst_ms2;    /* hysteresis margin on both zone boundaries */
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
    .coarse_ms2 = 2.5f,               \
    .level_ms2 = 0.5f,                \
    .zone_hyst_ms2 = 0.3f,            \
}

struct tilt_state {
    struct tilt_cal cal;
    enum tilt_mode last_mode;
    int last_sector;    /* last COARSE outer position 0..11 (-1 = none) */
    int last_quadrant;  /* last FINE inner cardinal 0..3   (-1 = none) */
};

/* Convenience initializer for a fresh state with the default calibration. */
#define TILT_STATE_INIT {                 \
    .cal = TILT_CAL_DEFAULT,              \
    .last_mode = TILT_MODE_LEVEL,         \
    .last_sector = -1,                    \
    .last_quadrant = -1,                  \
}

/* Stateless: TILT_LEVEL if in-plane |g| < cal->enter_ms2, else compass sector
 * 0..11 (downhill direction). Used for the pure-angle unit tests. */
int tilt_sector(float ax, float ay, const struct tilt_cal *cal);

/* Stateful zoom level: classifies the tilt magnitude into LEVEL/FINE/COARSE
 * with zone hysteresis, and within FINE/COARSE quantizes the downhill bearing
 * (4 or 12 sectors) with seam hysteresis. Updates *st. */
struct tilt_out tilt_update(struct tilt_state *st, float ax, float ay);

#endif /* PHIAL_TILT_H_ */
