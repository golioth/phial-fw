/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_APP_SETTINGS_H_
#define PHIAL_APP_SETTINGS_H_

#include <zephyr/device.h>

/* Give the settings module the LED device to drive. Call once at startup. */
void app_settings_init(const struct device *leds);

/* Current 1-based LED index (1..16), or 0 if none set yet. For `status`. */
int app_settings_led_index(void);

#endif /* PHIAL_APP_SETTINGS_H_ */
