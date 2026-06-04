/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <errno.h>

#include "env.h"

LOG_MODULE_REGISTER(env, LOG_LEVEL_INF);

#define ENV_SAMPLE_MS      10000
#define ENV_THREAD_STACK   1024
#define ENV_THREAD_PRIO    7

static const struct device *const bme = DEVICE_DT_GET(DT_NODELABEL(bme280));

static struct env_reading g_reading;     /* guarded by g_lock */
K_MUTEX_DEFINE(g_lock);

static void env_thread(void *p1, void *p2, void *p3);

/* SYS_FOREVER_MS start delay: the thread is NOT auto-started at boot
 * (K_THREAD_DEFINE's delay arg is a millisecond count; SYS_FOREVER_MS is the
 * sentinel for "never"). env_init() starts it only after the device-ready
 * check passes. */
K_THREAD_DEFINE(env_tid, ENV_THREAD_STACK, env_thread, NULL, NULL, NULL,
                ENV_THREAD_PRIO, 0, SYS_FOREVER_MS);

bool env_get(struct env_reading *out)
{
    bool valid;

    k_mutex_lock(&g_lock, K_FOREVER);
    *out = g_reading;
    valid = g_reading.valid;
    k_mutex_unlock(&g_lock);
    return valid;
}

static void env_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (1) {
        struct sensor_value t, h, p;

        if (sensor_sample_fetch(bme) == 0 &&
            sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &t) == 0 &&
            sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &h) == 0 &&
            sensor_channel_get(bme, SENSOR_CHAN_PRESS, &p) == 0) {

            float temp = (float)sensor_value_to_double(&t);
            float hum  = (float)sensor_value_to_double(&h);
            float pres = (float)sensor_value_to_double(&p);

            k_mutex_lock(&g_lock, K_FOREVER);
            g_reading.temp_c       = temp;
            g_reading.humidity_pct = hum;
            g_reading.pressure_kpa = pres;
            g_reading.uptime_ms    = k_uptime_get();
            g_reading.valid        = true;
            k_mutex_unlock(&g_lock);

            LOG_INF("T=%.2f C  RH=%.2f %%  P=%.2f kPa",
                    (double)temp, (double)hum, (double)pres);
        } else {
            LOG_WRN("BME280 fetch failed");
        }

        k_msleep(ENV_SAMPLE_MS);
    }
}

int env_init(void)
{
    static bool started;

    if (started) {
        return 0;
    }
    if (!device_is_ready(bme)) {
        LOG_ERR("BME280 not ready");
        return -ENODEV;
    }
    k_thread_start(env_tid);
    started = true;
    return 0;
}

/* ---- env shell command: print the latest cached reading ---------------- */

static int cmd_env(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    struct env_reading r;

    if (!env_get(&r)) {
        shell_print(sh, "no sample yet");
        return 0;
    }
    shell_print(sh, "env: T=%.2f C  RH=%.2f %%  P=%.2f kPa  (age %lld ms)",
                (double)r.temp_c, (double)r.humidity_pct, (double)r.pressure_kpa,
                (long long)(k_uptime_get() - r.uptime_ms));
    return 0;
}

SHELL_CMD_REGISTER(env, NULL, "Show latest BME280 environmental reading", cmd_env);
