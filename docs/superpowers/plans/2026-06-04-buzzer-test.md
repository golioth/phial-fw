# Buzzer Test-Tone App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new `app/buzzer-test` that plays half-octave test tones (800→8000 Hz) on the P0.04 buzzer via a `buzzer` shell command.

**Architecture:** PWM can't reach P0 on the nRF54L (power-domain split), so tones are a CPU-driven GPIO square wave: `buzzer.c` toggles P0.04 with `k_busy_wait(half_period)` (blocking). A `buzzer` shell command (`sweep`, `tone <hz> [ms]`) drives it; `main()` just inits and idles. New app mirrors the `led-test` skeleton.

**Tech Stack:** Zephyr RTOS, nRF54L15 (`nrf54l15dk/nrf54l15/cpuapp`), GPIO API (`gpio_dt_spec`, `zephyr,user` node), Zephyr shell. No PWM, no sensors, no new Kconfig beyond `CONFIG_GPIO`.

**Design spec:** [docs/superpowers/specs/2026-06-04-buzzer-test-design.md](../specs/2026-06-04-buzzer-test-design.md)

---

## Note on testing approach (read first)

Per the spec, no host unit test: the only pure logic is `half_period_us = 1000000/(2*hz)` (one division), and the rest is GPIO/shell glue — consistent with how `env.c`/`mag.c` (also glue) carry no host tests. The verification gate per code task is a **clean pristine build**; final acceptance is **on-hardware/audible** (Task 4, the user's).

## Build & flash commands (used throughout)

`west` is the repo's `.venv` west; workspace root is `/Users/chrisg/golioth/phial-fw`, manifest app dir is `phial-app`. Activate the venv (`source /Users/chrisg/golioth/phial-fw/.venv/bin/activate`) or call `/Users/chrisg/golioth/phial-fw/.venv/bin/west` directly.

```bash
# Build (pristine) — from the phial-app directory:
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/buzzer-test

# Flash (Task 4):
west flash
```
A pristine Zephyr build can take several minutes — use a generous Bash timeout (e.g. 600000 ms). All `git` runs from `/Users/chrisg/golioth/phial-fw/phial-app` (the git root). Work is on `master` (user-approved).

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `app/buzzer-test/CMakeLists.txt` | Build config | Create |
| `app/buzzer-test/prj.conf` | App Kconfig | Create |
| `app/buzzer-test/sample.yaml` | Twister metadata | Create |
| `app/buzzer-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Per-app overlay | Create (includes phial-common.dtsi) |
| `app/buzzer-test/src/main.c` | Startup glue | Create |
| `boards/phial-common.dtsi` | Shared hardware | Add `buzzer-gpios` to the existing `zephyr,user` node |
| `app/buzzer-test/src/buzzer.h` | Buzzer module interface | Create |
| `app/buzzer-test/src/buzzer.c` | GPIO tone/sweep + `buzzer` shell cmd | Create |

---

## Task 1: Scaffold the buzzer-test app (builds, shell comes up)

**Files:**
- Create: `app/buzzer-test/CMakeLists.txt`
- Create: `app/buzzer-test/prj.conf`
- Create: `app/buzzer-test/sample.yaml`
- Create: `app/buzzer-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay`
- Create: `app/buzzer-test/src/main.c`

- [ ] **Step 1: `CMakeLists.txt`**

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pull in the shared shell/log Kconfig before find_package(Zephyr).
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../../conf/shell-common.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(buzzer_test)

target_sources(app PRIVATE
    src/main.c
)
```

- [ ] **Step 2: `prj.conf`**

```conf
# Buzzer test app: GPIO square-wave tones on P0.04 via the shell.
CONFIG_GPIO=y

# Bringup diagnostic: Segger RTT as console + interactive shell, mirrors led-test.
# RTT runs over SWD so it works even if UART pinctrl is wrong for this board.
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
# Shell over RTT. shell-common.conf enables CONFIG_SHELL_BACKEND_SERIAL +
# CONFIG_SHELL_LOG_BACKEND, so logs route via the shell. Do NOT enable
# CONFIG_LOG_BACKEND_RTT (fights the shell over RTT channel 0) or a separate
# CONFIG_LOG_BACKEND_UART (would double-log on the UART).
CONFIG_SHELL_BACKEND_RTT=y
CONFIG_UART_CONSOLE=y

# This app does not enable CONFIG_SENSOR, so the shared BME280/LIS2DH nodes in
# phial-common.dtsi are inert here (their driver Kconfigs are sourced only under
# `if SENSOR`) — no sensor drivers are compiled in.
```

- [ ] **Step 3: `sample.yaml`**

```yaml
sample:
  name: Phial buzzer test
  description: Play GPIO square-wave test tones on the P0.04 buzzer via the shell.

common:
  build_only: true
  tags:
    - phial
    - buzzer
    - shell

tests:
  sample.phial.buzzer_test:
    platform_allow:
      - nrf54l15dk/nrf54l15/cpuapp
    integration_platforms:
      - nrf54l15dk/nrf54l15/cpuapp
```

- [ ] **Step 4: `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`**

```dts
/*
 * Per-app overlay wrapper: pull in the shared Phial hardware description.
 * Add app-specific tweaks below the include if ever needed; otherwise leave alone.
 */

#include "../../../boards/phial-common.dtsi"
```

- [ ] **Step 5: `src/main.c` (idle scaffold — no buzzer yet)**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Phial buzzer-test: shell up");
    return 0;
}
```

- [ ] **Step 6: Pristine build**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/buzzer-test
```
Expected: build **succeeds**. (`main()` returning is fine — the shell and other kernel threads keep running.)

- [ ] **Step 7: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/buzzer-test/CMakeLists.txt app/buzzer-test/prj.conf app/buzzer-test/sample.yaml app/buzzer-test/boards app/buzzer-test/src/main.c
git commit -m "feat(buzzer-test): scaffold new shell-driven app

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Add the buzzer GPIO to the shared devicetree

**Files:**
- Modify: `boards/phial-common.dtsi`

- [ ] **Step 1: Add `buzzer-gpios` to the EXISTING `zephyr,user` node**

In `boards/phial-common.dtsi`, the `zephyr,user` node already exists (added by the magnet feature) and currently contains only `mag-gpios`. Add a `buzzer-gpios` property to it — do NOT create a second `zephyr,user` node. Replace:

```dts
    zephyr,user {
        /* LF21115TMR omni-polar TMR magnet switch on P1.13 (former DK button0,
         * deleted above). Push-pull output: low = magnet present, so ACTIVE_LOW
         * makes the logical pin value 1 = present. No pull needed. */
        mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
    };
```

with:

```dts
    zephyr,user {
        /* LF21115TMR omni-polar TMR magnet switch on P1.13 (former DK button0,
         * deleted above). Push-pull output: low = magnet present, so ACTIVE_LOW
         * makes the logical pin value 1 = present. No pull needed. */
        mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
        /* ~4 kHz buzzer (transducer) on P0.04 (former DK button3, deleted above).
         * PWM can't reach P0 on nRF54L (power-domain split), so app/buzzer-test
         * drives it via CPU GPIO toggle. */
        buzzer-gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
    };
```

- [ ] **Step 2: Pristine build (node is valid; still unreferenced)**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/buzzer-test
```
Expected: build **succeeds**.

- [ ] **Step 3: Confirm the property landed in the generated devicetree**

```bash
grep -i "buzzer-gpios" /Users/chrisg/golioth/phial-fw/phial-app/build/zephyr/zephyr.dts
```
Expected: a `buzzer-gpios = < &gpio0 0x4 0x0 >;` line (pin 4, flags 0 = ACTIVE_HIGH).

- [ ] **Step 4: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add boards/phial-common.dtsi
git commit -m "feat(buzzer-test): add P0.04 buzzer GPIO to shared devicetree

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Implement the buzzer module and wire it into main()

**Files:**
- Create: `app/buzzer-test/src/buzzer.h`
- Create: `app/buzzer-test/src/buzzer.c`
- Modify: `app/buzzer-test/CMakeLists.txt`
- Modify: `app/buzzer-test/src/main.c`

- [ ] **Step 1: Write `buzzer.h`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_BUZZER_H_
#define PHIAL_BUZZER_H_

#include <stdint.h>

/* Configure the buzzer GPIO (P0.04) as an output, idle LOW.
 * Returns 0 on success, -ENODEV if the GPIO is not ready. */
int buzzer_init(void);

/* Play a square-wave tone at `hz` for `ms` milliseconds (blocking).
 * No-op when hz == 0 or before a successful buzzer_init(). The pin is left
 * LOW afterward. */
void buzzer_tone(uint32_t hz, uint32_t ms);

/* Play the fixed half-octave sweep (800..8000 Hz), blocking. */
void buzzer_sweep(void);

#endif /* PHIAL_BUZZER_H_ */
```

- [ ] **Step 2: Write `buzzer.c`**

```c
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
```

- [ ] **Step 3: Add `buzzer.c` to the build**

In `app/buzzer-test/CMakeLists.txt`, extend `target_sources`:

```cmake
target_sources(app PRIVATE
    src/main.c
    src/buzzer.c
)
```

- [ ] **Step 4: Wire `buzzer_init()` into `main.c`**

Replace the entire contents of `app/buzzer-test/src/main.c` with:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buzzer.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    if (buzzer_init() != 0) {
        LOG_WRN("buzzer disabled (GPIO unavailable)");
    }
    LOG_INF("Phial buzzer-test: try 'buzzer sweep' or 'buzzer tone <hz> [ms]'");
    return 0;
}
```

- [ ] **Step 5: Pristine build**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/buzzer-test
```
Expected: build **succeeds**, no warnings.

- [ ] **Step 6: Commit**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add app/buzzer-test/src/buzzer.c app/buzzer-test/src/buzzer.h app/buzzer-test/CMakeLists.txt app/buzzer-test/src/main.c
git commit -m "feat(buzzer-test): GPIO square-wave tones + buzzer shell command

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

- [ ] **Step 2: Sweep**

At the shell, run `buzzer sweep`. Expected: eight rising tones, audibly stepping up, each ~250 ms with a short gap; console logs `buzzer: 800 Hz` … `buzzer: 8000 Hz`. Loudest near 4 kHz (resonance), fainter at 800 Hz and 8000 Hz.

- [ ] **Step 3: Single tones**

`buzzer tone 4000` → a clear ~4 kHz tone (should be loudest). `buzzer tone 800` and `buzzer tone 8000` → audible but weaker. `buzzer tone 2000 1000` → a 2 kHz tone for 1 second.

- [ ] **Step 4: Idle/edge behavior**

Confirm the buzzer is silent between/after tones (pin idles LOW). `buzzer tone 0` or `buzzer tone 99999` → a usage error, no sound. The shell is briefly unresponsive during a tone/sweep (expected — busy-wait blocks the shell thread), then returns.

---

## Done criteria

- All three code tasks build pristine and are committed.
- On hardware: `buzzer sweep` plays the eight rising half-octave tones; `buzzer tone <hz>` plays single pitches; the buzzer is silent at idle; bad input is rejected.
