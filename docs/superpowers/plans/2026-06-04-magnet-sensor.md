# LF21115TMR Magnet Switch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Littelfuse LF21115TMR magnet switch on P1.13 to `app/sensor-test`: while a magnet is present, flash all 16 LEDs at 1 Hz and log each toggle; otherwise the existing tilt→LED display runs silently.

**Architecture:** A thin `mag.c`/`mag.h` GPIO module exposes `mag_init()`/`mag_present()` (a pin read — the future BLE/Golioth seam). The existing 50 Hz `main()` loop polls it each tick and arbitrates the LED device — blink-all when a magnet is present, tilt render otherwise — keeping ALL LED writes on the single main-loop thread (no concurrency).

**Tech Stack:** Zephyr RTOS, nRF54L15 (`nrf54l15dk/nrf54l15/cpuapp`), Zephyr GPIO API (`gpio_dt_spec`, `zephyr,user` node), no new Kconfig.

**Design spec:** [docs/superpowers/specs/2026-06-04-magnet-sensor-design.md](../specs/2026-06-04-magnet-sensor-design.md)

---

## Note on testing approach (read first)

Per the spec, this deviates from default TDD **by design**: `mag.c` is hardware glue (a GPIO read) and the loop change is display policy — there is no pure logic to unit-test the way `tilt.c` tests angle math. The verification gate for each code task is a **clean pristine build**; final acceptance is **on-hardware** (Task 4). Do not add a host unit test.

## Build & flash commands (used throughout)

`west` is the repo's `.venv` west; workspace root is `/Users/chrisg/golioth/phial-fw`, manifest app dir is `phial-app`. Activate the venv (`source /Users/chrisg/golioth/phial-fw/.venv/bin/activate`) or call `/Users/chrisg/golioth/phial-fw/.venv/bin/west` directly.

```bash
# Build (pristine) — from the phial-app directory:
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test

# Flash (Task 4):
west flash
```
A pristine Zephyr build can take several minutes — use a generous Bash timeout (e.g. 600000 ms). All `git` runs from `/Users/chrisg/golioth/phial-fw/phial-app` (the git root). Work is on `master` (user-approved).

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `boards/phial-common.dtsi` | Shared hardware description | Add `zephyr,user { mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>; }` |
| `app/sensor-test/src/mag.h` | Magnet module interface | Create — `mag_init`, `mag_present` |
| `app/sensor-test/src/mag.c` | Magnet GPIO config + read | Create |
| `app/sensor-test/CMakeLists.txt` | Build sources | Add `src/mag.c` |
| `app/sensor-test/src/main.c` | Display arbiter | Poll magnet each tick; blink-all + log when present; restore tilt on release |

---

## Task 1: Add the magnet GPIO to devicetree

**Files:**
- Modify: `boards/phial-common.dtsi`

- [ ] **Step 1: Add a `zephyr,user` node with the magnet GPIO**

In `boards/phial-common.dtsi`, add the following top-level node. A natural spot is right after the existing root `/ { ... };` LED block near the end of the file (any top-level location is fine; do not nest it inside another node):

```dts
/ {
    zephyr,user {
        /* LF21115TMR omni-polar TMR magnet switch on P1.13 (former DK button0,
         * deleted above). Push-pull output: low = magnet present, so ACTIVE_LOW
         * makes the logical pin value 1 = present. No pull needed. */
        mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
    };
};
```

- [ ] **Step 2: Pristine build (the node alone must not break the build)**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**. (Nothing references the property yet; this just adds the node.)

- [ ] **Step 3: Confirm the node + property landed in the generated devicetree**

```bash
grep -A3 "zephyr,user" /Users/chrisg/golioth/phial-fw/phial-app/build/zephyr/zephyr.dts | grep -i "mag-gpios"
```
Expected: a `mag-gpios` line referencing gpio1 pin 13 appears.

- [ ] **Step 4: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add boards/phial-common.dtsi
git commit -m "feat(sensor-test): add LF21115TMR magnet GPIO (P1.13) to devicetree

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Create the `mag` module

**Files:**
- Create: `app/sensor-test/src/mag.h`
- Create: `app/sensor-test/src/mag.c`
- Modify: `app/sensor-test/CMakeLists.txt`

- [ ] **Step 1: Write `mag.h`**

Create `app/sensor-test/src/mag.h` with EXACTLY:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_MAG_H_
#define PHIAL_MAG_H_

#include <stdbool.h>

/* Configure the LF21115TMR magnet-switch GPIO (P1.13) as an input.
 * Returns 0 on success, -ENODEV if the GPIO is not ready (feature disabled). */
int mag_init(void);

/* True when a magnet is present. The LF21115TMR pulls its push-pull output low
 * in a field; the DT spec is GPIO_ACTIVE_LOW, so the logical pin value is 1 then.
 * This is the seam a future BLE/Golioth transport reads. */
bool mag_present(void);

#endif /* PHIAL_MAG_H_ */
```

- [ ] **Step 2: Write `mag.c`**

Create `app/sensor-test/src/mag.c` with EXACTLY:

```c
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
```

- [ ] **Step 3: Add `mag.c` to the build**

In `app/sensor-test/CMakeLists.txt`, add `src/mag.c` to the `target_sources(app PRIVATE ...)` list. After the edit it reads:

```cmake
target_sources(app PRIVATE
    src/main.c
    src/tilt.c
    src/env.c
    src/mag.c
)
```

- [ ] **Step 4: Pristine build to verify the module compiles and links**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**. (`mag_init`/`mag_present` are defined but not yet called — fine; they are non-static so there is no unused-function warning.)

- [ ] **Step 5: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/sensor-test/src/mag.c app/sensor-test/src/mag.h app/sensor-test/CMakeLists.txt
git commit -m "feat(sensor-test): add mag module (LF21115TMR GPIO read)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Integrate the magnet override into `main()`

**Files:**
- Modify: `app/sensor-test/src/main.c`

- [ ] **Step 1: Include the header**

In `app/sensor-test/src/main.c`, add the include next to the existing `#include "env.h"` (around line 13):

```c
#include "tilt.h"
#include "env.h"
#include "mag.h"
```

- [ ] **Step 2: Call `mag_init()` at startup**

After the existing `env_init()` block (lines 70–72) and before the `LOG_INF("Phial sensor-test: ...")` line, add:

```c
    if (mag_init() != 0) {
        LOG_WRN("magnet sensor disabled (LF21115TMR GPIO unavailable)");
    }
```

- [ ] **Step 3: Replace the main loop with the magnet-aware version**

Replace the ENTIRE current loop block (the `while (1) { ... }` at lines 78–99, NOT the `render(leds, g_target);` line above it or the `return 0;` below it) with:

```c
    bool in_magnet_mode = false;   /* magnet override is currently driving the LEDs */
    bool blink_on = false;         /* current blink phase (re-init on entry) */
    int64_t last_blink_ms = 0;     /* uptime of last blink toggle (re-init on entry) */

    while (1) {
        struct sensor_value v[3];
        struct tilt_out t = g_target;   /* keep last target if a fetch fails */

        if (sensor_sample_fetch(accel) == 0 &&
            sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
            float ax = (float)sensor_value_to_double(&v[0]);
            float ay = (float)sensor_value_to_double(&v[1]);
            float az = (float)sensor_value_to_double(&v[2]);

            /* EMA low-pass to kill jitter. Keep sampling even in magnet mode so
             * the tilt target is current the instant the magnet leaves. */
            g_ax += g_alpha * (ax - g_ax);
            g_ay += g_alpha * (ay - g_ay);
            g_az += g_alpha * (az - g_az);

            t = tilt_update(&g_state, g_ax, g_ay);
        }

        if (mag_present()) {
            /* Magnet overrides the display: flash all 16 LEDs at 1 Hz. */
            if (!in_magnet_mode) {
                in_magnet_mode = true;
                blink_on = false;       /* first toggle below turns LEDs ON */
                last_blink_ms = k_uptime_get() - 500;  /* due now: immediate first toggle */
            }
            if (k_uptime_get() - last_blink_ms >= 500) {
                last_blink_ms = k_uptime_get();
                blink_on = !blink_on;
                for (int i = 0; i < NUM_LEDS; i++) {
                    if (blink_on) {
                        led_on(leds, i);
                    } else {
                        led_off(leds, i);
                    }
                }
                LOG_INF("magnet: LEDs %s", blink_on ? "ON" : "OFF");
            }
        } else if (in_magnet_mode) {
            /* Magnet just removed: leave blink mode, restore the tilt display. */
            in_magnet_mode = false;
            g_target = t;
            render(leds, t);
        } else if (t.mode != g_target.mode || t.sector != g_target.sector) {
            g_target = t;
            render(leds, t);
        }

        k_msleep(SAMPLE_MS);
    }
```

- [ ] **Step 4: Pristine build**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/sensor-test
```
Expected: build **succeeds**, no warnings about `t`, `az`, or unused variables.

- [ ] **Step 5: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/sensor-test/src/main.c
git commit -m "feat(sensor-test): magnet flashes all LEDs, overrides tilt display

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: On-hardware verification

**Files:** none (manual acceptance). No commit unless a fix is needed.

- [ ] **Step 1: Flash**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west flash
```

- [ ] **Step 2: Magnet present → flash + log**

Bring a magnet near the LF21115TMR. Expected: all 16 LEDs flash on/off at ~1 Hz (500 ms each), and the console prints `magnet: LEDs ON` then `magnet: LEDs OFF` on each toggle. First toggle should turn LEDs ON essentially immediately on detection.

- [ ] **Step 3: Magnet absent → tilt resumes, silent**

Remove the magnet. Expected: flashing stops, the `magnet:` logging stops, and the tilt→LED display resumes — tilt the board and confirm the ring/center LEDs respond again (and `tilt status` still works).

- [ ] **Step 4: Other sensors unaffected**

Confirm the BME280 `env:` line still prints every ~10 s throughout (including during magnet flashing), and `env` / `tilt status` shell commands still work. Confirms the env thread and accel sampling were not disturbed.

---

## Done criteria

- All three code tasks build pristine and are committed.
- On hardware: magnet near ⇒ all LEDs blink 1 Hz + `magnet: LEDs ON/OFF` logs; magnet removed ⇒ tilt display resumes and magnet logging stops; BME280 env logging continues throughout.
