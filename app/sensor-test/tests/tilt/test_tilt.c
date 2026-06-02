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
    const struct tilt_cal id = TILT_CAL_DEFAULT;
    check_eq(TILT_LEVEL, tilt_sector(0.0f, 0.0f, &id), "flat -> level");
    check_eq(TILT_LEVEL, tilt_sector(1.0f, 0.5f, &id), "small tilt -> level");
}

static void test_cardinals(void)
{
    const struct tilt_cal id = TILT_CAL_DEFAULT;
    check_eq(0, tilt_sector(0.0f, -G, &id), "North (ay=-g)");
    check_eq(3, tilt_sector(-G, 0.0f, &id), "East (ax=-g)");
    check_eq(6, tilt_sector(0.0f,  G, &id), "South (ay=+g)");
    check_eq(9, tilt_sector( G, 0.0f, &id), "West (ax=+g)");
}

static void test_diagonal_resolves_to_one_sector(void)
{
    const struct tilt_cal id = TILT_CAL_DEFAULT;
    float dx = 0.866f * G, dy = 0.5f * G;   /* downhill vector, compass 60 */
    check_eq(2, tilt_sector(-dx, -dy, &id), "compass 60 -> sector 2");
}

static void test_offset_rotates(void)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.offset_deg = 90.0f;
    check_eq(3, tilt_sector(0.0f, -G, &c), "N reading + 90 offset -> E");
}

static void test_invert_x(void)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.invert_x = true;
    check_eq(9, tilt_sector(-G, 0.0f, &c), "invert_x: E reading -> W");
}

static void test_swap_xy(void)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.swap_xy = true;
    check_eq(0, tilt_sector(-G, 0.0f, &c), "swap_xy: E reading -> N");
}

static void test_deadzone_hysteresis(void)
{
    struct tilt_state st = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };

    /* Below enter -> stays level. */
    check_eq(TILT_LEVEL, tilt_update(&st, -2.0f, 0.0f), "below enter -> level");
    /* Above enter -> rolls to East. */
    check_eq(3, tilt_update(&st, -G, 0.0f), "above enter -> East");
    /* Between exit and enter while rolled -> stays East (no snap back). */
    check_eq(3, tilt_update(&st, -2.0f, 0.0f), "in band while rolled -> hold East");
    /* Below exit -> returns to level. */
    check_eq(TILT_LEVEL, tilt_update(&st, -1.0f, 0.0f), "below exit -> level");
}

static void test_seam_hysteresis(void)
{
    struct tilt_state st = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };
    float ax, ay;

    /* Land squarely in East (sector 3, compass 90). */
    check_eq(3, tilt_update(&st, -G, 0.0f), "land East");

    /* Compass 108: raw sector 4, but within 15+seam(6)=21deg of sector 3's
     * center (90), |108-90|=18 -> holds at 3. Exercises the hold branch. */
    accel_for_compass(108.0f, &ax, &ay);
    check_eq(3, tilt_update(&st, ax, ay), "within seam band holds East");

    /* Compass 120 (sector 4 center, 30deg away, past band) -> releases to 4. */
    accel_for_compass(120.0f, &ax, &ay);
    check_eq(4, tilt_update(&st, ax, ay), "past seam band releases to 4");
}

int main(void)
{
    test_flat_is_level();
    test_cardinals();
    test_diagonal_resolves_to_one_sector();
    test_offset_rotates();
    test_invert_x();
    test_swap_xy();
    test_deadzone_hysteresis();
    test_seam_hysteresis();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
