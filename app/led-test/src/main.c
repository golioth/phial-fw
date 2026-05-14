/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BLINK_LED_IDX  0   /* led0 alias → LED01 on P1.10 */
#define BLINK_PERIOD_MS  500

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }

    LOG_INF("Phial first-light: blinking LED %d every %d ms",
            BLINK_LED_IDX, BLINK_PERIOD_MS);

    bool on = false;
    while (1) {
        on = !on;
        if (on) {
            led_on(leds, BLINK_LED_IDX);
        } else {
            led_off(leds, BLINK_LED_IDX);
        }
        k_msleep(BLINK_PERIOD_MS);
    }

    return 0;
}
