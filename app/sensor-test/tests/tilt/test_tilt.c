/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-native unit tests for the pure tilt logic.
 *
 * Zephyr's native_sim/POSIX target only builds on Linux, but tilt.c is
 * dependency-free C, so we test it directly with the system compiler. Runs on
 * macOS and Linux/CI alike.
 *
 * Build + run (from this directory):
 *   cc -std=c11 -Wall -I ../../src test_tilt.c ../../src/tilt.c -lm -o /tmp/tilt_test && /tmp/tilt_test
 */
#include <stdio.h>
#include <math.h>
#include "tilt.h"

/* M_PI is POSIX, not ISO C — define it for strict -std=c11 on Linux CI. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define G 9.80665f

/* Identity calibration — tests pin the pure math independent of the
 * board-specific offset baked into TILT_CAL_DEFAULT. */
#define CAL_ID { \
    .offset_deg = 0.0f, .swap_xy = false, .invert_x = false, .invert_y = false, \
    .enter_ms2 = 2.5f, .exit_ms2 = 1.5f, .seam_margin_deg = 6.0f, \
    .coarse_ms2 = 2.5f, .level_ms2 = 0.5f, .zone_hyst_ms2 = 0.3f, \
}

#define ST_ID() { .cal = CAL_ID, .last_mode = TILT_MODE_LEVEL, \
                  .last_sector = -1, .last_quadrant = -1 }

static int g_checks;
static int g_failures;

static void check_eq(int expected, int actual, const char *what)
{
    g_checks++;
    if (expected != actual) {
        g_failures++;
        printf("  FAIL: %-40s expected %d, got %d\n", what, expected, actual);
    }
}

/* downhill bearing `compass` deg -> (ax,ay) with |g|=G.
 * downhill vector (-ax,-ay) = (cos,sin) of the math angle (90 - compass). */
static void accel_for_compass(float compass_deg, float *ax, float *ay)
{
    float mr = (90.0f - compass_deg) * (float)M_PI / 180.0f;
    *ax = -cosf(mr) * G;
    *ay = -sinf(mr) * G;
}

static void test_flat_is_level(void)
{
    const struct tilt_cal id = CAL_ID;
    check_eq(TILT_LEVEL, tilt_sector(0.0f, 0.0f, &id), "flat -> level");
    check_eq(TILT_LEVEL, tilt_sector(1.0f, 0.5f, &id), "small tilt -> level");
}

static void test_cardinals(void)
{
    const struct tilt_cal id = CAL_ID;
    check_eq(0, tilt_sector(0.0f, -G, &id), "North (ay=-g)");
    check_eq(3, tilt_sector(-G, 0.0f, &id), "East (ax=-g)");
    check_eq(6, tilt_sector(0.0f,  G, &id), "South (ay=+g)");
    check_eq(9, tilt_sector( G, 0.0f, &id), "West (ax=+g)");
}

static void test_diagonal_resolves_to_one_sector(void)
{
    const struct tilt_cal id = CAL_ID;
    float dx = 0.866f * G, dy = 0.5f * G;   /* downhill vector, compass 60 */
    check_eq(2, tilt_sector(-dx, -dy, &id), "compass 60 -> sector 2");
}

static void test_offset_rotates(void)
{
    struct tilt_cal c = CAL_ID;
    c.offset_deg = 90.0f;
    check_eq(3, tilt_sector(0.0f, -G, &c), "N reading + 90 offset -> E");
}

static void test_invert_x(void)
{
    struct tilt_cal c = CAL_ID;
    c.invert_x = true;
    check_eq(9, tilt_sector(-G, 0.0f, &c), "invert_x: E reading -> W");
}

static void test_swap_xy(void)
{
    struct tilt_cal c = CAL_ID;
    c.swap_xy = true;
    check_eq(0, tilt_sector(-G, 0.0f, &c), "swap_xy: E reading -> N");
}

/* --- zones: magnitude picks LEVEL / FINE / COARSE; sector is correct --- */
static void test_zones(void)
{
    struct tilt_out o;

    struct tilt_state lvl = ST_ID();
    o = tilt_update(&lvl, 0.2f, 0.0f);          /* m=0.2 < level(0.5) */
    check_eq(TILT_MODE_LEVEL, o.mode, "tiny tilt -> LEVEL");

    struct tilt_state fine = ST_ID();
    o = tilt_update(&fine, -1.5f, 0.0f);        /* level<m<coarse, East */
    check_eq(TILT_MODE_FINE, o.mode, "mid tilt -> FINE");
    check_eq(1, o.sector, "FINE East -> quadrant 1");

    struct tilt_state coarse = ST_ID();
    o = tilt_update(&coarse, -G, 0.0f);         /* m>=coarse, East */
    check_eq(TILT_MODE_COARSE, o.mode, "big tilt -> COARSE");
    check_eq(3, o.sector, "COARSE East -> sector 3");
}

/* --- zone boundaries resist chatter (hysteresis) --- */
static void test_zone_hysteresis(void)
{
    struct tilt_state st = ST_ID();
    struct tilt_out o;

    o = tilt_update(&st, -1.5f, 0.0f);  /* -> FINE */
    check_eq(TILT_MODE_FINE, o.mode, "enter FINE");
    o = tilt_update(&st, -0.6f, 0.0f);  /* 0.6 > level-hyst(0.2): stays FINE */
    check_eq(TILT_MODE_FINE, o.mode, "above level-hyst holds FINE");
    o = tilt_update(&st, -0.1f, 0.0f);  /* below 0.2 -> LEVEL */
    check_eq(TILT_MODE_LEVEL, o.mode, "below level-hyst -> LEVEL");

    o = tilt_update(&st, -1.5f, 0.0f);  /* back to FINE */
    check_eq(TILT_MODE_FINE, o.mode, "re-enter FINE");
    o = tilt_update(&st, -2.6f, 0.0f);  /* 2.6 < coarse+hyst(2.8): stays FINE */
    check_eq(TILT_MODE_FINE, o.mode, "below coarse+hyst holds FINE");
    o = tilt_update(&st, -3.0f, 0.0f);  /* > 2.8 -> COARSE */
    check_eq(TILT_MODE_COARSE, o.mode, "above coarse+hyst -> COARSE");
}

/* --- coarse seam hysteresis (12-way) still holds within COARSE --- */
static void test_coarse_seam_hysteresis(void)
{
    struct tilt_state st = ST_ID();
    struct tilt_out o;
    float ax, ay;

    accel_for_compass(90.0f, &ax, &ay);          /* |g|=G -> COARSE, East */
    o = tilt_update(&st, ax, ay);
    check_eq(3, o.sector, "land COARSE East");

    accel_for_compass(108.0f, &ax, &ay);         /* within seam band -> hold 3 */
    o = tilt_update(&st, ax, ay);
    check_eq(3, o.sector, "coarse seam holds 3");

    accel_for_compass(120.0f, &ax, &ay);         /* past band -> release to 4 */
    o = tilt_update(&st, ax, ay);
    check_eq(4, o.sector, "coarse seam releases to 4");
}

/* --- fine quadrant maps each cardinal (downhill) to 0=N,1=E,2=S,3=W --- */
static void test_fine_quadrants(void)
{
    struct tilt_out o;
    struct tilt_state n = ST_ID(); o = tilt_update(&n, 0.0f, -1.5f);
    check_eq(TILT_MODE_FINE, o.mode, "N is FINE"); check_eq(0, o.sector, "FINE North -> 0");
    struct tilt_state e = ST_ID(); o = tilt_update(&e, -1.5f, 0.0f);
    check_eq(1, o.sector, "FINE East -> 1");
    struct tilt_state s = ST_ID(); o = tilt_update(&s, 0.0f, 1.5f);
    check_eq(2, o.sector, "FINE South -> 2");
    struct tilt_state w = ST_ID(); o = tilt_update(&w, 1.5f, 0.0f);
    check_eq(3, o.sector, "FINE West -> 3");
}

int main(void)
{
    test_flat_is_level();
    test_cardinals();
    test_diagonal_resolves_to_one_sector();
    test_offset_rotates();
    test_invert_x();
    test_swap_xy();
    test_zones();
    test_zone_hysteresis();
    test_coarse_seam_hysteresis();
    test_fine_quadrants();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
