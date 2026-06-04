/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "mag.h"

LOG_MODULE_REGISTER(mag, LOG_LEVEL_INF);

static const struct gpio_dt_spec mag = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), mag_gpios);

int mag_init(void)
{
    int rc;

    if (!gpio_is_ready_dt(&mag)) {
        LOG_ERR("magnet GPIO not ready");
        return -ENODEV;
    }

    rc = gpio_pin_configure_dt(&mag, GPIO_INPUT);
    if (rc != 0) {
        LOG_ERR("magnet GPIO configure failed (%d)", rc);
        return rc;
    }
    return 0;
}

bool mag_present(void)
{
    /* gpio_pin_get_dt returns the logical level (ACTIVE_LOW-corrected): 1 when a
     * magnet pulls the line low. Negative on error -> treated as "not present". */
    return gpio_pin_get_dt(&mag) == 1;
}
