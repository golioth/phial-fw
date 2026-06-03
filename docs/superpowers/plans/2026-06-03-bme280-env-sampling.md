# BME280 Environmental Sampling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add periodic (10 s) BME280 temperature/humidity/pressure logging plus an `env` shell command to `app/sensor-test`, without touching the accel→LED loop.

**Architecture:** A self-contained `env.c`/`env.h` module owns the BME280 device, a dedicated sampling thread (started only after a device-ready check), a mutex-guarded latest-reading cache, and the `env` shell command. `main()` gains one `env_init()` call. The sensor node is added to the shared devicetree. `env_get()` is the seam a future BLE/Golioth transport will read.

**Tech Stack:** Zephyr RTOS, nRF54L15 (`nrf54l15dk/nrf54l15/cpuapp`), Bosch BME280 driver (`CONFIG_BME280`), Zephyr sensor + shell subsystems, I²C (`i2c30`).

**Design spec:** [docs/superpowers/specs/2026-06-03-bme280-env-sampling-design.md](../specs/2026-06-03-bme280-env-sampling-design.md)

---

## Note on testing approach (read first)

This plan deviates from default TDD **by design, per the spec**: `env.c` is hardware glue over the Zephyr sensor API (which already returns calibrated real-world units), so there is no pure logic to unit-test the way `tilt.c` tests its angle math. The verification gate for each code task is therefore **a clean pristine build**, and final acceptance is **on-hardware observation** (Task 5). Do not add a host unit test for `env.c` — it would exercise the framework, not our code.

## Build & flash commands (used throughout)

`west` is the repo's `.venv` west; the workspace root is `/Users/chrisg/golioth/phial-fw` and the manifest app dir is `phial-app`. Activate the venv first (`source /Users/chrisg/golioth/phial-fw/.venv/bin/activate`) or call `/Users/chrisg/golioth/phial-fw/.venv/bin/west` directly.

```bash
# Build (pristine) — run from the phial-app directory:
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test

# Flash (Task 5):
west flash
```

All `git` commands run from `/Users/chrisg/golioth/phial-fw/phial-app` (that is the git root).

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `boards/phial-common.dtsi` | Shared hardware description | Add `bme280@76` node under `&i2c30` |
| `app/sensor-test/prj.conf` | App Kconfig | Add `CONFIG_BME280=y` |
| `app/sensor-test/src/env.h` | Env module public interface | Create — `struct env_reading`, `env_init`, `env_get` |
| `app/sensor-test/src/env.c` | Env module implementation | Create — device, thread, cache, `env` shell cmd |
| `app/sensor-test/CMakeLists.txt` | Build sources | Add `src/env.c` |
| `app/sensor-test/src/main.c` | App startup glue | Add `env_init()` call |

---

## Task 1: Instantiate the BME280 in devicetree and enable the driver

**Files:**
- Modify: `boards/phial-common.dtsi` (add node after the `lis2dh12@18` node)
- Modify: `app/sensor-test/prj.conf`

- [ ] **Step 1: Add the BME280 devicetree node**

In `boards/phial-common.dtsi`, immediately after the existing `accel: lis2dh12@18 { ... };` block (which is inside an `&i2c30 { ... }` overlay), add a new `&i2c30` overlay block:

```dts
/* BME280 temperature / humidity / pressure sensor on the Phial sensor bus.
 * Single compatible (no dual-compatible quirk like the LIS2DH12). NOTE:
 * CONFIG_BME280 is `default y` when this node is present, so the driver builds
 * into every app that includes this dtsi (not just sensor-test). Harmless —
 * a few KB of flash and an unread device. Set CONFIG_BME280=n in an app's
 * prj.conf if footprint matters there. */
&i2c30 {
    bme280: bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
        status = "okay";
    };
};
```

- [ ] **Step 2: Enable the driver explicitly in sensor-test**

In `app/sensor-test/prj.conf`, under the `# Sensor bus + LIS2DH12` group, add an explicit enable (it would default-y anyway, but be explicit about intent):

```conf
# BME280 temp/humidity/pressure
CONFIG_BME280=y
```

- [ ] **Step 3: Pristine build to verify the node binds and the driver compiles**

Run:
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**. The BME280 driver is now compiled in (because the node is present + `CONFIG_BME280=y`), even though nothing reads it yet.

- [ ] **Step 4: Confirm the node made it into the generated devicetree**

Run:
```bash
grep -i "bme280" /Users/chrisg/golioth/phial-fw/phial-app/build/zephyr/zephyr.dts
```
Expected: a `bme280@76` node with `compatible = "bosch,bme280"` and `status = "okay"` appears.

- [ ] **Step 5: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add boards/phial-common.dtsi app/sensor-test/prj.conf
git commit -m "feat(sensor-test): add BME280 devicetree node and enable driver

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Create the env module header

**Files:**
- Create: `app/sensor-test/src/env.h`

- [ ] **Step 1: Write `env.h`**

Create `app/sensor-test/src/env.h` with exactly:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_ENV_H_
#define PHIAL_ENV_H_

#include <stdbool.h>
#include <stdint.h>

/* Latest BME280 environmental reading. Units match what the Zephyr BME280
 * driver reports: temperature in °C, humidity in %RH, pressure in kPa. */
struct env_reading {
    float   temp_c;
    float   humidity_pct;
    float   pressure_kpa;
    int64_t uptime_ms;   /* k_uptime_get() when this sample was taken */
    bool    valid;       /* false until the first successful sample */
};

/* Verify the BME280 is ready and start the 10 s sampling thread.
 * Returns 0 on success, -ENODEV if the device is not ready (thread NOT
 * started, so a missing sensor produces no recurring warnings). */
int env_init(void);

/* Copy the latest cached reading into *out under the module mutex.
 * Returns true if a valid sample exists, false otherwise. This is the seam a
 * future BLE/Golioth transport reads — it never touches the bus or blocks. */
bool env_get(struct env_reading *out);

#endif /* PHIAL_ENV_H_ */
```

- [ ] **Step 2: Commit** (header only — no build impact yet)

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/sensor-test/src/env.h
git commit -m "feat(sensor-test): add env module interface (env.h)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Implement the env module

**Files:**
- Create: `app/sensor-test/src/env.c`
- Modify: `app/sensor-test/CMakeLists.txt`

- [ ] **Step 1: Write `env.c`**

Create `app/sensor-test/src/env.c` with exactly:

```c
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

            LOG_INF("env: T=%.2f C  RH=%.2f %%  P=%.2f kPa",
                    (double)temp, (double)hum, (double)pres);
        } else {
            LOG_WRN("BME280 fetch failed");
        }

        k_msleep(ENV_SAMPLE_MS);
    }
}

int env_init(void)
{
    if (!device_is_ready(bme)) {
        LOG_ERR("BME280 not ready");
        return -ENODEV;
    }
    k_thread_start(env_tid);
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
```

- [ ] **Step 2: Add `env.c` to the build**

In `app/sensor-test/CMakeLists.txt`, add `src/env.c` to the `target_sources` list:

```cmake
target_sources(app PRIVATE
    src/main.c
    src/tilt.c
    src/env.c
)
```

- [ ] **Step 3: Pristine build to verify the module compiles and links**

Run:
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**. (`env_init` is defined but not yet called — that is fine; it is non-`static` so there is no unused-function warning. The `env` shell command is registered at link time.)

- [ ] **Step 4: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/sensor-test/src/env.c app/sensor-test/CMakeLists.txt
git commit -m "feat(sensor-test): implement env module (BME280 thread + env cmd)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Wire `env_init()` into `main()`

**Files:**
- Modify: `app/sensor-test/src/main.c`

- [ ] **Step 1: Include the header**

In `app/sensor-test/src/main.c`, add the include next to the existing `#include "tilt.h"`:

```c
#include "tilt.h"
#include "env.h"
```

- [ ] **Step 2: Call `env_init()` at startup**

In `main()`, after the accelerometer ready check (the `if (!device_is_ready(accel)) { ... }` block) and before the `LOG_INF("Phial sensor-test: ...")` line, add:

```c
    if (env_init() != 0) {
        LOG_WRN("env sampling disabled (BME280 unavailable)");
    }
```

Rationale: env is non-critical to the LED demo, so a missing/failed BME280 only warns — `main()` continues and the accel→LED loop runs normally.

- [ ] **Step 3: Pristine build**

Run:
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**.

- [ ] **Step 4: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/sensor-test/src/main.c
git commit -m "feat(sensor-test): start env sampling from main()

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: On-hardware verification

**Files:** none (manual acceptance). No commit unless a fix is needed.

Prerequisite: board connected via J-Link; console reachable (RTT or the J-Link VCOM serial console per the project's serial-console notes).

- [ ] **Step 1: Flash**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west flash
```

- [ ] **Step 2: Verify periodic logging**

Open the console. Expected: an `env: T=... C  RH=... %  P=... kPa` line approximately every 10 s. Temperature should read a plausible room value (~20–28 °C), pressure ~95–102 kPa, humidity a plausible %RH.

- [ ] **Step 3: Verify the `env` shell command**

At the shell prompt, run `env`.
Expected: one line with the latest reading and an `(age NNNN ms)` under ~10000. Within the first 10 s after boot (before the first sample), it should instead print `no sample yet`.

- [ ] **Step 4: Verify responsiveness**

Breathe on / gently warm the board. Over the next one or two samples, humidity (and slightly, temperature) should rise, then relax. Confirms live data, not a frozen cache.

- [ ] **Step 5: Regression — LEDs unaffected**

Tilt the board and confirm the existing tilt→LED behavior is unchanged (single downhill ring LED when tilted, 4 center LEDs when level), and the `tilt status` command still works. Confirms the env thread did not disturb the 50 Hz accel loop.

- [ ] **Step 6: (Optional) Mark the spec's Phase-2 BME280 item satisfied**

If desired, note in the bringup doc that the BME280 is now wired and read (it was previously parked as a Phase-2 item).

---

## Done criteria

- All four code tasks build pristine and are committed.
- On hardware: 10 s `env:` log lines appear, `env` command returns the cached reading with a sane age, values respond to breath, and tilt→LED behavior is unchanged.
