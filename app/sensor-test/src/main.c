/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "tilt.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NUM_LEDS     16
#define CENTER_FIRST 12      /* indices 12..15 = LED13..LED16 */
#define SAMPLE_MS    20      /* ~50 Hz */
#define EMA_ALPHA    0.3f

/* Compass sector (0=N, CW) -> 0-based LED index. Sector n sits at n-o'clock:
 * sector0/N->LED12(11), 3/E->LED03(2), 6/S->LED06(5), 9/W->LED09(8). */
static const uint8_t sector_to_led[12] = {
    11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
};

/* Shared so the `tilt` shell command can read live values. */
static struct tilt_state g_state = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };
static float g_ax, g_ay, g_az;   /* filtered, m/s^2 */
static int   g_target = TILT_LEVEL;

static void render(const struct device *leds, int target)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_off(leds, i);
    }
    if (target == TILT_LEVEL) {
        for (int i = CENTER_FIRST; i < NUM_LEDS; i++) {
            led_on(leds, i);
        }
    } else {
        led_on(leds, sector_to_led[target]);
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

    LOG_INF("Phial sensor-test: tilt drives LEDs (%d Hz)", 1000 / SAMPLE_MS);

    render(leds, TILT_LEVEL);   /* start showing 'level' */

    while (1) {
        struct sensor_value v[3];

        if (sensor_sample_fetch(accel) == 0 &&
            sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
            float ax = (float)sensor_value_to_double(&v[0]);
            float ay = (float)sensor_value_to_double(&v[1]);
            float az = (float)sensor_value_to_double(&v[2]);

            /* EMA low-pass to kill jitter. */
            g_ax += EMA_ALPHA * (ax - g_ax);
            g_ay += EMA_ALPHA * (ay - g_ay);
            g_az += EMA_ALPHA * (az - g_az);

            int target = tilt_update(&g_state, g_ax, g_ay);
            if (target != g_target) {
                g_target = target;
                render(leds, target);
            }
        }
        k_msleep(SAMPLE_MS);
    }
    return 0;
}
