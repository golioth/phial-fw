/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

    LOG_INF("Phial sensor-test: LED + LIS2DH12 ready");

    while (1) {
        k_msleep(1000);
    }
    return 0;
}
