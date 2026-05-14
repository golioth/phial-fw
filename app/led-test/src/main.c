/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }

    LOG_INF("Phial LED test booted, %d LEDs available",
            DT_CHILD_NUM(DT_PATH(leds)));
    return 0;
}
