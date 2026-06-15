/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include <pouch/golioth/settings_callbacks.h>

#include "app_settings.h"
#include "led_index.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

static const struct device *g_leds;
static int g_index;   /* 1-based; 0 = unset */

void app_settings_init(const struct device *leds)
{
    g_leds = leds;
}

int app_settings_led_index(void)
{
    return g_index;
}

/* Light LED `idx0` (0-based) and turn all others off. */
static void show_only(uint8_t idx0)
{
    if (g_leds == NULL) {
        return;
    }
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i == idx0) {
            led_on(g_leds, i);
        } else {
            led_off(g_leds, i);
        }
    }
}

/* Golioth "LED" setting (int). 1..16 lights that LED; out-of-range is logged
 * and ignored (prior state kept). */
static int led_setting_cb(int32_t new_value)
{
    uint8_t idx0;

    if (!led_index_decode(new_value, &idx0)) {
        LOG_WRN("LED setting %d out of range 1..%d; ignoring", (int)new_value, NUM_LEDS);
        return 0;   /* accept the delivery; just don't act on a bad value */
    }
    LOG_INF("LED setting -> %d", (int)new_value);
    show_only(idx0);
    g_index = (int)new_value;
    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);
