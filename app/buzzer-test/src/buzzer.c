/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdlib.h>

#include "buzzer.h"

LOG_MODULE_REGISTER(buzzer, LOG_LEVEL_INF);

#define TONE_MS_DEFAULT  250
#define SWEEP_GAP_MS     100
#define FREQ_MAX_HZ      20000

static const struct gpio_dt_spec buzzer =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), buzzer_gpios);

static bool init_ok;   /* true only after the pin is configured as output */

/* Half-octave (x sqrt(2)) series, 800 Hz up to an 8000 Hz ceiling. */
static const uint32_t sweep_hz[] = {
    800, 1131, 1600, 2263, 3200, 4525, 6400, 8000,
};

int buzzer_init(void)
{
    int rc;

    if (!gpio_is_ready_dt(&buzzer)) {
        LOG_ERR("buzzer GPIO not ready");
        return -ENODEV;
    }
    rc = gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
    if (rc != 0) {
        LOG_ERR("buzzer GPIO configure failed (%d)", rc);
        return rc;
    }
    init_ok = true;
    return 0;
}

void buzzer_tone(uint32_t hz, uint32_t ms)
{
    uint32_t half_us, toggles;

    if (!init_ok || hz == 0 || ms == 0) {
        return;
    }

    half_us = 1000000U / (2U * hz);
    if (half_us == 0) {
        return;                       /* frequency too high to represent */
    }
    toggles = (uint32_t)((uint64_t)ms * 1000U / half_us);

    for (uint32_t i = 0; i < toggles; i++) {
        gpio_pin_toggle_dt(&buzzer);
        k_busy_wait(half_us);
    }
    gpio_pin_set_dt(&buzzer, 0);      /* idle LOW regardless of toggle parity */
}

void buzzer_sweep(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(sweep_hz); i++) {
        LOG_INF("buzzer: %u Hz", sweep_hz[i]);
        buzzer_tone(sweep_hz[i], TONE_MS_DEFAULT);
        k_msleep(SWEEP_GAP_MS);
    }
}

/* ---- buzzer shell command ---------------------------------------------- */

static int cmd_buzzer_sweep(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    if (!init_ok) {
        shell_error(sh, "buzzer unavailable");
        return -ENODEV;
    }
    shell_print(sh, "sweeping 800..8000 Hz");
    buzzer_sweep();
    return 0;
}

static int cmd_buzzer_tone(const struct shell *sh, size_t argc, char **argv)
{
    long hz, ms;

    if (!init_ok) {
        shell_error(sh, "buzzer unavailable");
        return -ENODEV;
    }
    hz = strtol(argv[1], NULL, 10);
    ms = (argc > 2) ? strtol(argv[2], NULL, 10) : TONE_MS_DEFAULT;

    if (hz <= 0 || hz > FREQ_MAX_HZ) {
        shell_error(sh, "hz must be 1..%d", FREQ_MAX_HZ);
        return -EINVAL;
    }
    if (ms <= 0) {
        shell_error(sh, "ms must be > 0");
        return -EINVAL;
    }
    shell_print(sh, "tone %ld Hz for %ld ms", hz, ms);
    buzzer_tone((uint32_t)hz, (uint32_t)ms);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(buzzer_sub,
    SHELL_CMD(sweep, NULL, "Play the 800..8000 Hz half-octave sweep.",
              cmd_buzzer_sweep),
    SHELL_CMD_ARG(tone, NULL, "Play <hz> [ms] (default 250 ms).",
                  cmd_buzzer_tone, 2, 1),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(buzzer, &buzzer_sub, "Phial buzzer test tones", NULL);
