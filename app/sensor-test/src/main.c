/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <math.h>

#include "tilt.h"
#include "env.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NUM_LEDS   16
#define SAMPLE_MS  20        /* ~50 Hz */

/* Coarse: compass sector (0=N, CW) -> 0-based outer LED index. Sector n is at
 * n-o'clock: 0/N->LED12(11), 3/E->LED03(2), 6/S->LED06(5), 9/W->LED09(8). */
static const uint8_t sector_to_led[12] = {
    11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
};

/* Fine: cardinal quadrant (0=N,1=E,2=S,3=W) -> inner LED index.
 * LED13 N(12), LED14 E(13), LED15 S(14), LED16 W(15). Verify on hardware. */
static const uint8_t quadrant_to_led[4] = { 12, 13, 14, 15 };

/* Shared so the `tilt` shell command can read/tune live. */
static struct tilt_state g_state = TILT_STATE_INIT;
static float g_alpha = 0.5f;         /* EMA smoothing (higher = snappier) */
static float g_ax, g_ay, g_az;       /* filtered, m/s^2 */
static struct tilt_out g_target = { .mode = TILT_MODE_LEVEL, .sector = -1 };

static void render(const struct device *leds, struct tilt_out t)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_off(leds, i);
    }
    switch (t.mode) {
    case TILT_MODE_LEVEL:
        for (int i = 0; i < NUM_LEDS; i++) {
            led_on(leds, i);          /* level achieved: whole board solid */
        }
        break;
    case TILT_MODE_FINE:
        led_on(leds, quadrant_to_led[t.sector]);
        break;
    case TILT_MODE_COARSE:
        led_on(leds, sector_to_led[t.sector]);
        break;
    }
}

int main(void)
{
    const struct device *leds  = DEVICE_DT_GET_ANY(gpio_leds);
    const struct device *accel = DEVICE_DT_GET(DT_NODELABEL(accel));

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }
    if (!device_is_ready(accel)) {
        LOG_ERR("Accelerometer (lis2dh12) not ready");
        return -ENODEV;
    }

    if (env_init() != 0) {
        LOG_WRN("env sampling disabled (BME280 unavailable)");
    }

    LOG_INF("Phial sensor-test: zoom level (%d Hz)", 1000 / SAMPLE_MS);

    render(leds, g_target);   /* start at LEVEL (all on) until first sample */

    while (1) {
        struct sensor_value v[3];

        if (sensor_sample_fetch(accel) == 0 &&
            sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
            float ax = (float)sensor_value_to_double(&v[0]);
            float ay = (float)sensor_value_to_double(&v[1]);
            float az = (float)sensor_value_to_double(&v[2]);

            /* EMA low-pass to kill jitter. */
            g_ax += g_alpha * (ax - g_ax);
            g_ay += g_alpha * (ay - g_ay);
            g_az += g_alpha * (az - g_az);

            struct tilt_out t = tilt_update(&g_state, g_ax, g_ay);
            if (t.mode != g_target.mode || t.sector != g_target.sector) {
                g_target = t;
                render(leds, t);
            }
        }
        k_msleep(SAMPLE_MS);
    }
    return 0;
}

/* ---- tilt shell command: live status + tuning ------------------------- */

static int cmd_tilt_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    struct tilt_out t = g_target;
    float mag = hypotf(g_ax, g_ay);

    shell_print(sh, "accel (filt): x=%.3f y=%.3f z=%.3f  |xy|=%.3f m/s^2",
                (double)g_ax, (double)g_ay, (double)g_az, (double)mag);
    shell_print(sh, "offset=%.1f deg  alpha=%.2f  coarse>=%.2f  level<%.2f  hyst=%.2f",
                (double)g_state.cal.offset_deg, (double)g_alpha,
                (double)g_state.cal.coarse_ms2, (double)g_state.cal.level_ms2,
                (double)g_state.cal.zone_hyst_ms2);

    switch (t.mode) {
    case TILT_MODE_LEVEL:
        shell_print(sh, "mode: LEVEL (all 16 on)");
        break;
    case TILT_MODE_FINE:
        shell_print(sh, "mode: FINE  quadrant %d -> LED%02d (inner)",
                    t.sector, quadrant_to_led[t.sector] + 1);
        break;
    case TILT_MODE_COARSE:
        shell_print(sh, "mode: COARSE sector %d -> LED%02d (outer)",
                    t.sector, sector_to_led[t.sector] + 1);
        break;
    }
    return 0;
}

static int cmd_tilt_offset(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "usage: tilt offset <deg>");
        return -EINVAL;
    }
    g_state.cal.offset_deg = strtof(argv[1], NULL);
    shell_print(sh, "offset = %.1f deg (volatile)", (double)g_state.cal.offset_deg);
    return 0;
}

static int cmd_tilt_coarse(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "usage: tilt coarse <m/s^2>");
        return -EINVAL;
    }
    g_state.cal.coarse_ms2 = strtof(argv[1], NULL);
    shell_print(sh, "coarse threshold = %.2f m/s^2", (double)g_state.cal.coarse_ms2);
    return 0;
}

static int cmd_tilt_level(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "usage: tilt level <m/s^2>");
        return -EINVAL;
    }
    g_state.cal.level_ms2 = strtof(argv[1], NULL);
    shell_print(sh, "level threshold = %.2f m/s^2", (double)g_state.cal.level_ms2);
    return 0;
}

static int cmd_tilt_alpha(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "usage: tilt alpha <0..1>");
        return -EINVAL;
    }
    g_alpha = strtof(argv[1], NULL);
    shell_print(sh, "EMA alpha = %.2f", (double)g_alpha);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tilt_sub,
    SHELL_CMD(status, NULL, "Show filtered accel, magnitude, zone config, target.",
              cmd_tilt_status),
    SHELL_CMD_ARG(offset, NULL, "Set angle offset in degrees (volatile).",
                  cmd_tilt_offset, 2, 0),
    SHELL_CMD_ARG(coarse, NULL, "Set COARSE zone threshold, m/s^2 (volatile).",
                  cmd_tilt_coarse, 2, 0),
    SHELL_CMD_ARG(level, NULL, "Set LEVEL zone threshold, m/s^2 (volatile).",
                  cmd_tilt_level, 2, 0),
    SHELL_CMD_ARG(alpha, NULL, "Set EMA smoothing 0..1, higher=snappier (volatile).",
                  cmd_tilt_alpha, 2, 0),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tilt, &tilt_sub, "Phial tilt/level tuning", NULL);
