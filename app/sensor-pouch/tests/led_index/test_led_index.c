/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-native unit test for the pure LED-index decoder.
 * Build + run (from this dir):
 *   cc -std=c11 -Wall -I ../../src test_led_index.c ../../src/led_index.c -o /tmp/li_test && /tmp/li_test
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "led_index.h"

static int checks, failures;
static void expect(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("FAIL: %s\n", what); }
}

int main(void)
{
    uint8_t idx = 0xAA;

    /* valid 1..16 -> 0..15 */
    expect(led_index_decode(1, &idx) && idx == 0,  "1 -> 0");
    expect(led_index_decode(16, &idx) && idx == 15, "16 -> 15");
    expect(led_index_decode(7, &idx) && idx == 6,  "7 -> 6");

    /* out of range -> false, idx untouched */
    idx = 42;
    expect(!led_index_decode(0, &idx) && idx == 42,  "0 rejected");
    expect(!led_index_decode(17, &idx) && idx == 42, "17 rejected");
    expect(!led_index_decode(-1, &idx) && idx == 42, "-1 rejected");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
