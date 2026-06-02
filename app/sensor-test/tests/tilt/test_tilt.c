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

int main(void)
{
    test_flat_is_level();
    test_cardinals();
    test_diagonal_resolves_to_one_sector();
    test_offset_rotates();
    test_invert_x();
    test_swap_xy();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
