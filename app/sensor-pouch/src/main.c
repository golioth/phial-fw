/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <string.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/transport/bluetooth/gatt.h>

#include "ble_peripheral.h"
#include "credentials.h"
#include "env.h"
#include "app_settings.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_gpios);
static struct gpio_callback button_cb;

static const struct device *g_leds;
static atomic_t g_syncing;
/* g_last_sync: pointer to a string literal, written from the workqueue/pouch-event
 * contexts and read from the shell. Word-aligned pointer load/store is atomic on
 * this target and the value is display-only, so no lock is needed. */
static const char *g_last_sync = "none";
static struct k_work g_sync_work;

static void do_uplink(void)
{
    struct env_reading r;
    char json[96];

    if (!env_get(&r)) {
        LOG_WRN("no BME280 reading yet; skipping uplink");
        return;
    }
    int n = snprintf(json, sizeof(json),
                     "{\"temp\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f}",
                     (double)r.temp_c, (double)r.humidity_pct, (double)r.pressure_kpa);
    if (n <= 0 || n >= (int)sizeof(json)) {
        LOG_ERR("uplink JSON encode failed");
        return;
    }
    int err = pouch_uplink_entry_write(".s/sensor", POUCH_CONTENT_TYPE_JSON,
                                       json, (size_t)n, POUCH_FOREVER);
    if (err) {
        LOG_ERR("uplink write failed (%d)", err);
    } else {
        LOG_INF("uplink: %s", json);
    }
}
POUCH_UPLINK_HANDLER(do_uplink);

static void on_pouch_event(enum pouch_event event, void *ctx)
{
    ARG_UNUSED(ctx);
    if (event == POUCH_EVENT_SESSION_END) {
        ble_peripheral_request_gateway(false);
        atomic_set(&g_syncing, 0);
        g_last_sync = "ok";
        LOG_INF("sync complete");
    }
}
POUCH_EVENT_HANDLER(on_pouch_event, NULL);

static void start_sync(void)
{
    if (!atomic_cas(&g_syncing, 0, 1)) {
        return;   /* a sync is already in progress */
    }
    g_last_sync = "in-progress";
    LOG_INF("sync requested");
    ble_peripheral_request_gateway(true);
}

static void sync_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    start_sync();
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    k_work_submit(&g_sync_work);
}

static int setup_pouch(void)
{
    struct pouch_config config = {0};
    int err = load_certificate(&config.certificate);

    if (err) {
        LOG_ERR("No certificate (%d) - provision creds over MCUmgr, then reboot", err);
        return err;
    }
    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL) {
        LOG_ERR("No private key - provision creds over MCUmgr, then reboot");
        return -ENOENT;
    }
    err = pouch_init(&config);
    if (err) {
        LOG_ERR("pouch_init failed (%d)", err);
    }
    return err;
}

static void setup_button(void)
{
    if (!gpio_is_ready_dt(&button) ||
        gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP) != 0 ||
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE) != 0) {
        LOG_WRN("boot button unavailable; use the gateway's own trigger");
        return;
    }
    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    if (gpio_add_callback(button.port, &button_cb) != 0) {
        LOG_WRN("failed to add button callback");
    }
}

int main(void)
{
    LOG_INF("Phial sensor-pouch (pouch proto v%d, gatt v%d)",
            POUCH_VERSION, POUCH_GATT_VERSION);

    int err = ble_peripheral_init();
    if (err) {
        LOG_ERR("BLE init failed (%d)", err);
        return err;
    }

    bool pouch_ok = (setup_pouch() == 0);

    g_leds = DEVICE_DT_GET_ANY(gpio_leds);
    if (g_leds != NULL && device_is_ready(g_leds)) {
        app_settings_init(g_leds);
    } else {
        LOG_WRN("LED device not ready; LED setting will be logged only");
        g_leds = NULL;
    }

    if (env_init() != 0) {
        LOG_WRN("BME280 unavailable; uplink will have no reading");
    }

    k_work_init(&g_sync_work, sync_work_handler);
    setup_button();

    err = ble_peripheral_start();
    if (err) {
        LOG_ERR("BLE advertising start failed (%d)", err);
        return err;
    }
    LOG_INF("ready%s - press the boot button to sync",
            pouch_ok ? "" : " (no creds: provision over MCUmgr + reboot)");
    return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    struct env_reading r;
    bool have = env_get(&r);

    shell_print(sh, "syncing:   %s", atomic_get(&g_syncing) ? "yes" : "no");
    shell_print(sh, "last sync: %s", g_last_sync);
    shell_print(sh, "LED index: %d", app_settings_led_index());
    if (have) {
        shell_print(sh, "BME280:    T=%.2f C  RH=%.2f %%  P=%.2f kPa",
                    (double)r.temp_c, (double)r.humidity_pct, (double)r.pressure_kpa);
    } else {
        shell_print(sh, "BME280:    no reading yet");
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sensorpouch_sub,
    SHELL_CMD(status, NULL, "Show pouch/BLE state, LED index, latest reading.", cmd_status),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(sensorpouch, &sensorpouch_sub, "Phial sensor-pouch status", NULL);
