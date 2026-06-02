# sensor-test Tilt → LED Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `app/sensor-test`, a sibling to `app/led-test`, that turns the on-board LIS2DH12 accelerometer into a control plane for the 16 LEDs — a single ring LED lights on the downhill edge as you tilt, the 4 center LEDs mean "level."

**Architecture:** A pure, host-testable logic unit (`tilt.c`/`tilt.h`) converts an (ax, ay) reading into a display target (level, or compass sector 0–11). A ~50 Hz control loop in `main.c` owns the sensor + LED devices, low-pass filters the reading, calls the pure logic, and renders to the LEDs only on change. A `tilt` shell command aids bench calibration. The LIS2DH12 is described once in the shared `boards/phial-common.dtsi`.

**Tech Stack:** Zephyr / nRF Connect SDK 3.1.1, nRF54L15 (Cortex-M33), Zephyr `st,lis2dh` sensor driver, ztest on `native_sim`, west (in repo `.venv`).

**Spec:** `docs/superpowers/specs/2026-06-02-sensor-test-tilt-led-design.md`

---

## Conventions for all tasks

- **Run commands from the workspace root** `/Users/chrisg/golioth/phial-fw` unless stated otherwise. West lives in the repo venv: use `.venv/bin/west`.
- **Board build:** `-b nrf54l15dk/nrf54l15/cpuapp`, build dir `build/sensor-test`. **Always pass `--no-sysbuild`** — these apps are secure-only with no MCUboot, and Zephyr's default sysbuild wrapper fails to configure them. (All `west build` commands below assume this flag even where omitted for brevity.)
- **LED indexing is 0-based** (driver index = silkscreen label − 1). LED01=0 … LED16=15.
- **Commit** at the end of each task with the exact message given. Commit straight to `master` (matches this repo's workflow).

## File Structure

| File | Responsibility |
|------|----------------|
| `phial-app/boards/phial-common.dtsi` (modify) | Add the shared `lis2dh12@18` node under `&i2c30` |
| `phial-app/app/sensor-test/CMakeLists.txt` (create) | App build: pull shell-common.conf, build `main.c` + `tilt.c` |
| `phial-app/app/sensor-test/prj.conf` (create) | App Kconfig (LED/I2C/SENSOR/LIS2DH/FPU/float-print/RTT) |
| `phial-app/app/sensor-test/sample.yaml` (create) | Twister metadata (build_only, board) |
| `phial-app/app/sensor-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` (create) | `#include` the shared overlay |
| `phial-app/app/sensor-test/src/tilt.h` (create) | Pure-logic interface + calibration struct |
| `phial-app/app/sensor-test/src/tilt.c` (create) | Pure logic: sector math + hysteresis |
| `phial-app/app/sensor-test/src/main.c` (create) | Control loop, render, `tilt` shell command |
| `phial-app/app/sensor-test/tests/tilt/*` (create) | ztest unit tests on `native_sim` |

---

## Task 1: Add the LIS2DH12 node to the shared overlay

**Files:**
- Modify: `phial-app/boards/phial-common.dtsi` (append a new `&i2c30` block near the existing I²C section)

- [ ] **Step 1: Add the sensor node**

Append after the existing `&i2c30 { … }` controller block in `phial-common.dtsi`:

```dts
/* LIS2DH12 3-axis accelerometer (U10) on the Phial sensor bus.
 * Dual compatible is REQUIRED: the Zephyr driver matches DT_DRV_COMPAT
 * `st_lis2dh` and CONFIG_LIS2DH depends on DT_HAS_ST_LIS2DH_ENABLED; a node
 * with only `st,lis2dh12` binds no driver. Inert for apps without
 * CONFIG_LIS2DH (e.g. led-test), so this stays in the shared overlay. */
&i2c30 {
    accel: lis2dh12@18 {
        compatible = "st,lis2dh12", "st,lis2dh";
        reg = <0x18>;
        status = "okay";
    };
};
```

- [ ] **Step 2: Verify led-test still builds (no regression from the shared node)**

Run: `.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build/led-test phial-app/app/led-test`
Expected: build succeeds (the node is present but unused — led-test has no `CONFIG_LIS2DH`).

- [ ] **Step 3: Commit**

```bash
git -C phial-app add boards/phial-common.dtsi
git -C phial-app commit -m "phial-common.dtsi: add shared LIS2DH12 accel node on i2c30

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Scaffold `app/sensor-test` (builds + boots)

A minimal app that acquires the LED + accel devices and idles. No tilt logic yet — proves the build, devicetree binding, and device readiness.

**Files:**
- Create: `phial-app/app/sensor-test/CMakeLists.txt`
- Create: `phial-app/app/sensor-test/prj.conf`
- Create: `phial-app/app/sensor-test/sample.yaml`
- Create: `phial-app/app/sensor-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay`
- Create: `phial-app/app/sensor-test/src/main.c` (temporary skeleton; replaced in Task 5)

- [ ] **Step 1: CMakeLists.txt**

```cmake
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Pull in the shared shell/log Kconfig before find_package(Zephyr).
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../../conf/shell-common.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_test)

target_sources(app PRIVATE
    src/main.c
    src/tilt.c
)
```

> Note: `src/tilt.c` is listed now but created in Task 3. If you build before Task 3, temporarily comment the `src/tilt.c` line; Task 3 restores it. (Recommended: create an empty `src/tilt.c` with just `#include "tilt.h"` in Task 3 before building the app again in Task 5.)

- [ ] **Step 2: prj.conf**

```
# LED API + GPIO backend
CONFIG_LED=y
CONFIG_LED_GPIO=y
CONFIG_GPIO=y

# Sensor bus + LIS2DH12
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_LIS2DH=y

# Float angle math + printing it in the shell
CONFIG_FPU=y
CONFIG_CBPRINTF_FP_SUPPORT=y

CONFIG_MAIN_STACK_SIZE=2048

# Bringup diagnostic: Segger RTT as console + shell, mirrors led-test.
# RTT runs over SWD so it works even if UART pinctrl is wrong for this board.
# shell-common.conf already enables CONFIG_SHELL_BACKEND_SERIAL + CONFIG_LOG_BACKEND_UART.
# Do NOT also enable CONFIG_LOG_BACKEND_RTT — it would fight the shell over RTT channel 0.
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
CONFIG_SHELL_BACKEND_RTT=y
CONFIG_UART_CONSOLE=y
```

- [ ] **Step 3: boards overlay** (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`)

```dts
/*
 * Per-app overlay wrapper: pull in the shared Phial hardware description
 * (LED map, i2c30, lis2dh12). Add app-specific tweaks below the include.
 */

#include "../../../boards/phial-common.dtsi"
```

- [ ] **Step 4: sample.yaml**

```yaml
sample:
  name: Phial sensor test
  description: LIS2DH12 tilt drives the 16 LEDs; exercises Zephyr sensor + shell.

common:
  build_only: true
  tags:
    - phial
    - sensor
    - accel

tests:
  sample.phial.sensor_test:
    platform_allow:
      - nrf54l15dk/nrf54l15/cpuapp
    integration_platforms:
      - nrf54l15dk/nrf54l15/cpuapp
```

- [ ] **Step 5: Skeleton main.c**

```c
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
```

- [ ] **Step 6: Create an empty `src/tilt.c` so the app links**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Filled in during Task 3. */
```

- [ ] **Step 7: Build**

Run: `.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build/sensor-test phial-app/app/sensor-test`
Expected: build succeeds; memory report prints.

- [ ] **Step 8 (optional, hardware): Flash + confirm boot**

Run: `.venv/bin/west flash -d build/sensor-test`
Expected on the console (115200 8N1): boot banner + `phial:~$`, and `Phial sensor-test: LED + LIS2DH12 ready` in the log. If the accel line errors, run `i2c scan i2c@104000` (expect 0x18) and `device list` (expect `lis2dh12@18`).

- [ ] **Step 9: Commit**

```bash
git -C phial-app add app/sensor-test
git -C phial-app commit -m "app/sensor-test: skeleton builds + boots, acquires LED + LIS2DH12

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Pure logic — `tilt_sector` + calibration (TDD)

The host-testable core. Converts (ax, ay) → `TILT_LEVEL` or compass sector 0–11, applying calibration (axis swap/invert, offset). **Downhill semantics:** a static accelerometer reports the up/reaction vector, so the downhill direction is `(-ax, -ay)`.

**Files:**
- Create: `phial-app/app/sensor-test/src/tilt.h`
- Create/replace: `phial-app/app/sensor-test/src/tilt.c`
- Create: `phial-app/app/sensor-test/tests/tilt/CMakeLists.txt`
- Create: `phial-app/app/sensor-test/tests/tilt/prj.conf`
- Create: `phial-app/app/sensor-test/tests/tilt/testcase.yaml`
- Create: `phial-app/app/sensor-test/tests/tilt/src/main.c`

- [ ] **Step 1: tilt.h**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_TILT_H_
#define PHIAL_TILT_H_

#include <stdbool.h>

/* Returned by tilt_sector / tilt_update when the board is within the level
 * deadzone. Otherwise the return is a compass sector 0..11 (0 = North,
 * increasing clockwise). */
#define TILT_LEVEL (-1)

/* Calibration. Defaults assume the chip's +X = board East, +Y = board North,
 * +Z = up. Real mounting rotation/mirroring is dialed in on the bench, then
 * baked into TILT_CAL_DEFAULT. Thresholds are in m/s^2 of in-plane gravity
 * (|g| ~ 9.8; enter ~2.5 ≈ 15° tilt, exit ~1.5 ≈ 9°). */
struct tilt_cal {
    float offset_deg;       /* added to the compass angle (chip-vs-ring rotation) */
    bool  swap_xy;          /* swap axes if the chip is mounted rotated 90° */
    bool  invert_x;         /* per-axis sign flips for mirror/rotation */
    bool  invert_y;
    float enter_ms2;        /* in-plane |g| to leave level (show a ring LED) */
    float exit_ms2;         /* in-plane |g| to return to level (< enter = hysteresis) */
    float seam_margin_deg;  /* extra angle past a seam before the lit sector changes */
};

#define TILT_CAL_DEFAULT {            \
    .offset_deg = 0.0f,               \
    .swap_xy = false,                 \
    .invert_x = false,                \
    .invert_y = false,                \
    .enter_ms2 = 2.5f,                \
    .exit_ms2 = 1.5f,                 \
    .seam_margin_deg = 6.0f,          \
}

struct tilt_state {
    struct tilt_cal cal;
    int last;   /* last reported target: TILT_LEVEL or 0..11 */
};

/* Stateless: TILT_LEVEL if in-plane |g| < cal->enter_ms2, else compass sector
 * 0..11 (downhill direction). */
int tilt_sector(float ax, float ay, const struct tilt_cal *cal);

/* Stateful: like tilt_sector but adds enter/exit deadzone hysteresis and a
 * seam margin, both keyed off st->last, to prevent flicker. Updates st->last. */
int tilt_update(struct tilt_state *st, float ax, float ay);

#endif /* PHIAL_TILT_H_ */
```

- [ ] **Step 2: Unit-test harness files**

`tests/tilt/CMakeLists.txt`:
```cmake
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tilt_test)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/tilt.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src)
```

`tests/tilt/prj.conf`:
```
CONFIG_ZTEST=y
```

`tests/tilt/testcase.yaml`:
```yaml
tests:
  phial.tilt.unit:
    platform_allow: native_sim
    tags:
      - phial
      - sensor
      - unit
```

- [ ] **Step 3: Write the failing tests** (`tests/tilt/src/main.c`)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <math.h>          /* M_PI, cosf, sinf — used by the seam test in Task 4 */
#include "tilt.h"

#define G 9.80665f

static const struct tilt_cal id = TILT_CAL_DEFAULT;

ZTEST_SUITE(tilt, NULL, NULL, NULL, NULL, NULL);

/* --- deadzone --- */
ZTEST(tilt, test_flat_is_level)
{
    zassert_equal(TILT_LEVEL, tilt_sector(0.0f, 0.0f, &id));
    zassert_equal(TILT_LEVEL, tilt_sector(1.0f, 0.5f, &id)); /* |g| ~1.1 < enter */
}

/* --- cardinals (downhill = -ax,-ay). East edge down => ax = -g => sector 3. --- */
ZTEST(tilt, test_cardinals)
{
    zassert_equal(0, tilt_sector(0.0f, -G, &id), "North");  /* ay=-g -> N */
    zassert_equal(3, tilt_sector(-G, 0.0f, &id), "East");   /* ax=-g -> E */
    zassert_equal(6, tilt_sector(0.0f,  G, &id), "South");  /* ay=+g -> S */
    zassert_equal(9, tilt_sector( G, 0.0f, &id), "West");   /* ax=+g -> W */
}

/* --- a diagonal well inside a sector (NE-ish ~ compass 60 = sector 2) --- */
ZTEST(tilt, test_diagonal_resolves_to_one_sector)
{
    /* downhill bearing 60° (ENE): pick ax,ay so -ax,-ay points there.
     * compass 60° => math angle 30°; -ax,-ay = (cos30, sin30)*g. */
    float dx = 0.866f * G, dy = 0.5f * G;     /* downhill vector */
    int s = tilt_sector(-dx, -dy, &id);
    zassert_equal(2, s, "expected sector 2, got %d", s);
}

/* --- calibration: +90° offset rotates North reading to East sector --- */
ZTEST(tilt, test_offset_rotates)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.offset_deg = 90.0f;
    zassert_equal(3, tilt_sector(0.0f, -G, &c), "N+90 -> E");
}

/* --- calibration: invert_x flips the East/West reading --- */
ZTEST(tilt, test_invert_x)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.invert_x = true;
    zassert_equal(9, tilt_sector(-G, 0.0f, &c), "invert_x: E reading -> W");
}

/* --- calibration: swap_xy exchanges the axes --- */
ZTEST(tilt, test_swap_xy)
{
    struct tilt_cal c = TILT_CAL_DEFAULT;
    c.swap_xy = true;
    /* with swap, ax=-g behaves like ay=-g (North) */
    zassert_equal(0, tilt_sector(-G, 0.0f, &c), "swap: E reading -> N");
}
```

- [ ] **Step 4: Run tests, verify they FAIL**

Run: `.venv/bin/west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt --inline-logs -c`
Expected: FAIL — `tilt_sector` returns 0 for everything (empty/stub `tilt.c`), assertions fail. (If `tilt.c` is empty, it is a link error instead; either counts as red. Prefer adding the stub from Task 2 Step 6 so it compiles and fails on assertions.)

- [ ] **Step 5: Implement `tilt_sector` in `tilt.c`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "tilt.h"
#include <math.h>

#define RAD_TO_DEG (180.0f / (float)M_PI)

/* Apply axis swap/invert, return in *ox,*oy. */
static void apply_axes(const struct tilt_cal *cal, float ax, float ay,
                       float *ox, float *oy)
{
    if (cal->swap_xy) {
        float t = ax; ax = ay; ay = t;
    }
    if (cal->invert_x) ax = -ax;
    if (cal->invert_y) ay = -ay;
    *ox = ax;
    *oy = ay;
}

/* Downhill bearing as a compass angle in [0,360): 0=N, increasing clockwise. */
static float downhill_compass_deg(const struct tilt_cal *cal, float ax, float ay)
{
    float cx, cy;
    apply_axes(cal, ax, ay, &cx, &cy);

    /* Downhill direction = -(in-plane reaction vector) = (-cx, -cy). */
    float math_deg = atan2f(-cy, -cx) * RAD_TO_DEG;   /* CCW from +x (East) */
    float compass = 90.0f - math_deg + cal->offset_deg;
    compass = fmodf(compass, 360.0f);
    if (compass < 0.0f) compass += 360.0f;
    return compass;
}

int tilt_sector(float ax, float ay, const struct tilt_cal *cal)
{
    float mag = hypotf(ax, ay);
    if (mag < cal->enter_ms2) {
        return TILT_LEVEL;
    }
    float compass = downhill_compass_deg(cal, ax, ay);
    int sector = (int)lroundf(compass / 30.0f);
    return ((sector % 12) + 12) % 12;
}

/* Defined in Task 4. */
int tilt_update(struct tilt_state *st, float ax, float ay)
{
    (void)st; (void)ax; (void)ay;
    return TILT_LEVEL;
}
```

- [ ] **Step 6: Run tests, verify they PASS**

Run: `.venv/bin/west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt --inline-logs -c`
Expected: PASS (the `tilt_update` cases come in Task 4).

- [ ] **Step 7: Commit**

```bash
git -C phial-app add app/sensor-test/src/tilt.h app/sensor-test/src/tilt.c app/sensor-test/tests
git -C phial-app commit -m "sensor-test: pure tilt_sector logic + calibration, host-tested

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Pure logic — `tilt_update` hysteresis (TDD)

Adds deadzone (enter/exit) and seam hysteresis so the display does not flicker at boundaries.

**Files:**
- Modify: `phial-app/app/sensor-test/tests/tilt/src/main.c` (add tests)
- Modify: `phial-app/app/sensor-test/src/tilt.c` (implement `tilt_update`)

- [ ] **Step 1: Add failing hysteresis tests** (append to `tests/tilt/src/main.c`)

```c
/* --- enter/exit deadzone hysteresis --- */
ZTEST(tilt, test_deadzone_hysteresis)
{
    struct tilt_state st = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };

    /* Below enter -> stays level. */
    zassert_equal(TILT_LEVEL, tilt_update(&st, -2.0f, 0.0f));

    /* Above enter -> rolls to East. */
    zassert_equal(3, tilt_update(&st, -G, 0.0f));

    /* Between exit and enter while already rolled -> stays East (no snap back). */
    zassert_equal(3, tilt_update(&st, -2.0f, 0.0f));

    /* Below exit -> returns to level. */
    zassert_equal(TILT_LEVEL, tilt_update(&st, -1.0f, 0.0f));
}

/* --- seam hysteresis: dither just past a seam holds; well past releases --- */
/* Helper: build (ax,ay) whose downhill bearing is `compass` degrees, |g|=G.
 * downhill vector (-ax,-ay) = (cos,sin) of the math angle (90 - compass). */
static void accel_for_compass(float compass_deg, float *ax, float *ay)
{
    float mr = (90.0f - compass_deg) * (float)M_PI / 180.0f;
    *ax = -cosf(mr) * G;
    *ay = -sinf(mr) * G;
}

ZTEST(tilt, test_seam_hysteresis)
{
    struct tilt_state st = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };
    float ax, ay;

    /* Land squarely in East (sector 3, compass 90). */
    zassert_equal(3, tilt_update(&st, -G, 0.0f));

    /* Compass 108: raw sector is 4 (round(108/30)=4), but it is within the
     * 15 + seam_margin(6) = 21deg hold band of sector 3's center (90), so the
     * lit sector MUST stay 3. This genuinely exercises the hold branch. */
    accel_for_compass(108.0f, &ax, &ay);
    zassert_equal(3, tilt_update(&st, ax, ay), "within seam band holds East");

    /* Compass 120 (sector 4 center, 30deg from sector 3 — past the band):
     * the lit sector now releases to 4. */
    accel_for_compass(120.0f, &ax, &ay);
    zassert_equal(4, tilt_update(&st, ax, ay), "past seam band releases to sector 4");
}
```

- [ ] **Step 2: Run tests, verify the new ones FAIL**

Run: `.venv/bin/west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt --inline-logs -c`
Expected: the two new tests FAIL (`tilt_update` is a stub returning `TILT_LEVEL`).

- [ ] **Step 3: Implement `tilt_update`** (replace the stub in `tilt.c`)

```c
int tilt_update(struct tilt_state *st, float ax, float ay)
{
    float mag = hypotf(ax, ay);
    const struct tilt_cal *cal = &st->cal;

    /* Deadzone with enter/exit hysteresis. */
    if (st->last == TILT_LEVEL) {
        if (mag < cal->enter_ms2) {
            return st->last;           /* stay level */
        }
    } else {
        if (mag < cal->exit_ms2) {
            st->last = TILT_LEVEL;     /* fall back to level */
            return st->last;
        }
    }

    float compass = downhill_compass_deg(cal, ax, ay);
    int sector = (((int)lroundf(compass / 30.0f)) % 12 + 12) % 12;

    /* Seam hysteresis: if we already show a ring LED, only switch sectors when
     * the angle has moved more than seam_margin past the midpoint between the
     * current sector center and the new one. Approximated as: keep last sector
     * unless the angular distance to the last sector center exceeds
     * 15deg + seam_margin. */
    if (st->last != TILT_LEVEL && sector != st->last) {
        float last_center = st->last * 30.0f;
        float d = fmodf(fabsf(compass - last_center), 360.0f);
        if (d > 180.0f) d = 360.0f - d;
        if (d <= (15.0f + cal->seam_margin_deg)) {
            sector = st->last;         /* within the sticky band, hold */
        }
    }

    st->last = sector;
    return sector;
}
```

> Note: `downhill_compass_deg` is `static` in `tilt.c`, so both functions share it — no header change needed.

- [ ] **Step 4: Run tests, verify ALL PASS**

Run: `.venv/bin/west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt --inline-logs -c`
Expected: all `tilt` tests PASS.

- [ ] **Step 5: Commit**

```bash
git -C phial-app add app/sensor-test/src/tilt.c app/sensor-test/tests/tilt/src/main.c
git -C phial-app commit -m "sensor-test: tilt_update enter/exit + seam hysteresis, host-tested

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Wire the control loop into `main.c`

Replace the skeleton with the real ~50 Hz loop: sample → filter → `tilt_update` → render only on change.

**Files:**
- Modify: `phial-app/app/sensor-test/src/main.c`

- [ ] **Step 1: Implement the control loop**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "tilt.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NUM_LEDS     16
#define CENTER_FIRST 12      /* indices 12..15 = LED13..LED16 */
#define SAMPLE_MS    20      /* ~50 Hz */
#define EMA_ALPHA    0.3f

/* Compass sector (0=N, CW) -> 0-based LED index. Sector n sits at n-o'clock:
 * sector0/N->LED12(11), 3/E->LED03(2), 6/S->LED06(5), 9/W->LED09(8). */
static const uint8_t sector_to_led[12] = {
    11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
};

/* Shared so the `tilt` shell command can read live values. */
static struct tilt_state g_state = { .cal = TILT_CAL_DEFAULT, .last = TILT_LEVEL };
static float g_ax, g_ay, g_az;   /* filtered, m/s^2 */
static int   g_target = TILT_LEVEL;

static void render(const struct device *leds, int target)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_off(leds, i);
    }
    if (target == TILT_LEVEL) {
        for (int i = CENTER_FIRST; i < NUM_LEDS; i++) {
            led_on(leds, i);
        }
    } else {
        led_on(leds, sector_to_led[target]);
    }
}

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

    LOG_INF("Phial sensor-test: tilt drives LEDs (%d Hz)", 1000 / SAMPLE_MS);

    render(leds, TILT_LEVEL);   /* start showing 'level' */

    while (1) {
        struct sensor_value v[3];

        if (sensor_sample_fetch(accel) == 0 &&
            sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
            float ax = (float)sensor_value_to_double(&v[0]);
            float ay = (float)sensor_value_to_double(&v[1]);
            float az = (float)sensor_value_to_double(&v[2]);

            /* EMA low-pass to kill jitter. */
            g_ax += EMA_ALPHA * (ax - g_ax);
            g_ay += EMA_ALPHA * (ay - g_ay);
            g_az += EMA_ALPHA * (az - g_az);

            int target = tilt_update(&g_state, g_ax, g_ay);
            if (target != g_target) {
                g_target = target;
                render(leds, target);
            }
        }
        k_msleep(SAMPLE_MS);
    }
    return 0;
}
```

- [ ] **Step 2: Build**

Run: `.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build/sensor-test phial-app/app/sensor-test`
Expected: build succeeds.

- [ ] **Step 3 (hardware): Flash + smoke check**

Run: `.venv/bin/west flash -d build/sensor-test`
Expected: flat board → 4 center LEDs (LED13–16) lit; tilting lights a single ring LED that moves with tilt. Direction may be rotated until calibrated (Task 7) — that's expected.

- [ ] **Step 4: Commit**

```bash
git -C phial-app add app/sensor-test/src/main.c
git -C phial-app commit -m "sensor-test: 50 Hz tilt control loop drives the 16 LEDs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `tilt` shell command (status + offset)

Bench-calibration aid: print live readings/angle/sector, and set a volatile offset.

**Files:**
- Modify: `phial-app/app/sensor-test/src/main.c` (add shell command + a small accessor)

- [ ] **Step 1: Add the shell command** (append to `main.c`, after `main()`)

```c
#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int cmd_tilt_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    int target = g_target;
    shell_print(sh, "accel (filt): x=%.3f y=%.3f z=%.3f m/s^2",
                (double)g_ax, (double)g_ay, (double)g_az);
    shell_print(sh, "offset=%.1f deg  enter=%.2f exit=%.2f",
                (double)g_state.cal.offset_deg,
                (double)g_state.cal.enter_ms2, (double)g_state.cal.exit_ms2);
    if (target == TILT_LEVEL) {
        shell_print(sh, "target: LEVEL (center cluster)");
    } else {
        shell_print(sh, "target: sector %d -> LED index %d (LED%02d)",
                    target, sector_to_led[target], sector_to_led[target] + 1);
    }
    return 0;
}

static int cmd_tilt_offset(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "usage: tilt offset <deg>");
        return -EINVAL;
    }
    g_state.cal.offset_deg = strtof(argv[1], NULL);
    shell_print(sh, "offset set to %.1f deg (volatile)",
                (double)g_state.cal.offset_deg);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tilt_sub,
    SHELL_CMD(status, NULL, "Show filtered accel, angle offset, and target LED.",
              cmd_tilt_status),
    SHELL_CMD_ARG(offset, NULL, "Set volatile angle offset in degrees.",
                  cmd_tilt_offset, 2, 0),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tilt, &tilt_sub, "Phial tilt-to-LED tuning", NULL);
```

- [ ] **Step 2: Build**

Run: `.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build/sensor-test phial-app/app/sensor-test`
Expected: build succeeds.

- [ ] **Step 3 (hardware): Verify the command**

Flash, then at `phial:~$`: `tilt status` prints values; tilt the board and re-run to watch the sector change; `tilt offset 30` then `tilt status` shows the new offset.

- [ ] **Step 4: Commit**

```bash
git -C phial-app add app/sensor-test/src/main.c
git -C phial-app commit -m "sensor-test: add tilt shell command (status + volatile offset)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Hardware bring-up, calibration, and bake offset

Manual, on-device. Resolves the chip-to-ring rotation and records it.

**Files:**
- Modify: `phial-app/app/sensor-test/src/tilt.h` (bake final calibration into `TILT_CAL_DEFAULT`)

- [ ] **Step 1: Sanity-check the raw sensor**

At `phial:~$`: `sensor get lis2dh12@18` → expect ≈ +9.8 on whichever axis points up when flat. If the device name differs, use the name from `device list`.

- [ ] **Step 2: Find the offset / axis mapping**

Tilt toward a known edge (e.g. the silkscreen "North"/LED12 corner). Run `tilt status`; note the reported sector vs the physically-lit LED. Adjust with `tilt offset <deg>` (and, if axes are swapped/mirrored, note that `swap_xy`/`invert_x`/`invert_y` are needed) until the lit LED matches the downhill edge in all four cardinals.

- [ ] **Step 3: Bake the result into `TILT_CAL_DEFAULT`**

Edit `tilt.h` — set `.offset_deg` (and any swap/invert flags) to the values found in Step 2. Leave thresholds unless the deadzone felt wrong.

- [ ] **Step 4: Re-run host unit tests** (guard against breaking the math)

Run: `.venv/bin/west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt --inline-logs -c`
Expected: PASS. (If a baked offset/swap changes expected sectors, update the affected test expectations to match the new default and note why.)

- [ ] **Step 5: Rebuild, flash, confirm all four cardinals + level**

Run: `.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build/sensor-test phial-app/app/sensor-test && .venv/bin/west flash -d build/sensor-test`
Expected: flat → center cluster; tilt N/E/S/W → LED12/03/06/09 (downhill); intermediate tilts → the in-between LED.

- [ ] **Step 6: Commit**

```bash
git -C phial-app add app/sensor-test/src/tilt.h app/sensor-test/tests
git -C phial-app commit -m "sensor-test: bake bench-calibrated tilt offset into defaults

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 7: Update the app README table** (optional, if `README.md` lists apps)

Add an `app/sensor-test/` row to the Applications table in `phial-app/README.md`, then commit.

---

## Done criteria

- `west twister -p native_sim -T phial-app/app/sensor-test/tests/tilt` is green.
- `app/sensor-test` builds for `nrf54l15dk/nrf54l15/cpuapp` and `app/led-test` still builds.
- On hardware: flat → center cluster; tilt → a single downhill ring LED tracking the tilt across all 12 positions; `tilt status` / `sensor get lis2dh12@18` work.
