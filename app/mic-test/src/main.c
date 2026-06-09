/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include "mic.h"
#include "store.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NUM_LEDS  16
#define POLL_MS   10

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_gpios);

/* 128 KB capture buffer in .noinit: not zero-initialized at boot and not part of
 * the image's initialized data — it is fully overwritten by each capture. */
static int16_t cap_buf[MIC_MAX_SAMPLES] __noinit;

static bool button_pressed(void)
{
    /* GPIO_ACTIVE_LOW in DT -> logical 1 when the button is held. <0 on error
     * is treated as "not pressed". */
    return gpio_pin_get_dt(&button) == 1;
}

static void leds_all(const struct device *leds, bool on)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        if (on) {
            led_on(leds, i);
        } else {
            led_off(leds, i);
        }
    }
}

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);
    bool mic_ok = false;

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&button) ||
        gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP) != 0) {
        LOG_ERR("boot button GPIO not ready");
        return -ENODEV;
    }

    mic_ok = (mic_init() == 0);
    if (!mic_ok) {
        LOG_WRN("mic disabled (PDM unavailable); button/LEDs still work");
    }

    leds_all(leds, false);
    LOG_INF("Phial mic-test: hold the boot button to record (max %u ms); "
            "release to store. 'mic info' to retrieve.",
            (unsigned)(MIC_MAX_SAMPLES * 1000U / MIC_SAMPLE_RATE));

    while (1) {
        if (!button_pressed()) {
            k_msleep(POLL_MS);
            continue;
        }

        /* Press: light the board and record until release or buffer full. */
        leds_all(leds, true);
        LOG_INF("recording...");

        size_t n = 0;

        if (mic_ok) {
            int rc = mic_capture(cap_buf, MIC_MAX_SAMPLES, button_pressed, &n);

            if (rc < 0) {
                LOG_WRN("capture error (%d); %zu samples salvaged", rc, n);
            } else if (n == MIC_MAX_SAMPLES) {
                LOG_INF("max length reached");
            }
        } else {
            /* No mic: hold the LEDs while pressed so the button still demos. */
            while (button_pressed()) {
                k_msleep(POLL_MS);
            }
        }

        leds_all(leds, false);

        if (n > 0) {
            int rc = store_save_wav(cap_buf, n, MIC_SAMPLE_RATE);

            if (rc < 0) {
                LOG_ERR("store failed (%d)", rc);
            } else {
                struct clip_info c;

                store_last(&c);
                LOG_INF("stored %u bytes @ 0x%06x (%u samples, %u ms) — "
                        "'mic info' for the readback command",
                        c.len, c.addr, c.samples,
                        c.samples * 1000U / MIC_SAMPLE_RATE);
            }
        } else if (mic_ok) {
            LOG_INF("nothing captured");
        }
    }
    return 0;
}
