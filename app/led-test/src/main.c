/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NUM_LEDS  16
#define DWELL_MS  50

/* Index → expected Phial label + nRF pin, for cross-checking against the PCB.
 * Order must match the children of `leds {}` in boards/phial-common.dtsi. */
static const char *const led_names[NUM_LEDS] = {
    "LED01 P1.10",
    "LED02 P1.09",
    "LED03 P0.03",
    "LED04 P0.02",
    "LED05 P2.06",
    "LED06 P2.05",
    "LED07 P2.04",
    "LED08 P2.03",
    "LED09 P2.02",
    "LED10 P2.01",
    "LED11 P2.00",
    "LED12 P1.14",
    "LED13 P2.08",
    "LED14 P2.09",
    "LED15 P2.10",
    "LED16 P2.07",
};

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }

    LOG_INF("Phial LED walk: %d LEDs, %d ms each", NUM_LEDS, DWELL_MS);

    for (int i = 0; i < NUM_LEDS; i++) {
        led_off(leds, i);
    }

    while (1) {
        for (int i = 0; i < NUM_LEDS; i++) {
            LOG_INF("idx=%d -> %s ON", i, led_names[i]);
            led_on(leds, i);
            k_msleep(DWELL_MS);
            led_off(leds, i);
        }
    }

    return 0;
}
