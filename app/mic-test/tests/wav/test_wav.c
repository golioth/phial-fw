/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-native unit test for the pure WAV header builder.
 *
 * wav.c is dependency-free C, so we test it with the system compiler (Zephyr's
 * native_sim/POSIX target only builds on Linux; this runs on macOS too).
 *
 * Build + run (from this directory):
 *   cc -std=c11 -Wall -I ../../src test_wav.c ../../src/wav.c -o /tmp/wav_test && /tmp/wav_test
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "wav.h"

static int g_checks;
static int g_failures;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void check_u32(uint32_t expected, uint32_t actual, const char *what)
{
    g_checks++;
    if (expected != actual) {
        g_failures++;
        printf("FAIL: %s: expected %u, got %u\n", what, expected, actual);
    }
}

static void check_tag(const uint8_t *p, const char *tag)
{
    g_checks++;
    if (memcmp(p, tag, 4) != 0) {
        g_failures++;
        printf("FAIL: tag: expected '%.4s', got '%.4s'\n", tag, (const char *)p);
    }
}

static void test_header(uint32_t samples, uint32_t rate)
{
    uint8_t h[WAV_HEADER_BYTES];
    uint32_t data = samples * 2;

    memset(h, 0xAA, sizeof(h));    /* poison: prove every byte is written */
    wav_header(h, samples, rate);

    check_tag(&h[0], "RIFF");
    check_u32(36 + data, rd32(&h[4]), "riff size");
    check_tag(&h[8], "WAVE");
    check_tag(&h[12], "fmt ");
    check_u32(16, rd32(&h[16]), "fmt size");
    check_u32(1,  rd16(&h[20]), "audio format");
    check_u32(1,  rd16(&h[22]), "num channels");
    check_u32(rate,     rd32(&h[24]), "sample rate");
    check_u32(rate * 2, rd32(&h[28]), "byte rate");
    check_u32(2,  rd16(&h[32]), "block align");
    check_u32(16, rd16(&h[34]), "bits per sample");
    check_tag(&h[36], "data");
    check_u32(data, rd32(&h[40]), "data size");
}

int main(void)
{
    test_header(16000, 16000);   /* 1 s */
    test_header(65512, 16000);   /* ~max clip */
    test_header(0, 16000);       /* empty clip: data size 0 */

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
