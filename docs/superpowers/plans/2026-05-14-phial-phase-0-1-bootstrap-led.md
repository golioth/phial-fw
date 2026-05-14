# Phial Phase 0 + 1 — Bootstrap & Minimal LED Bringup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the Phial firmware repo skeleton (Phase 0) and bring up all 16 onboard LEDs on the nRF54L15 with a working Zephyr shell (Phase 1), per `docs/superpowers/specs/2026-05-14-phial-bringup-design.md`.

**Architecture:** Single repo with multi-app layout (`app/<name>/`), Golioth RDT-flavored. T2 freestanding west workspace pulling NCS via `golioth-firmware-sdk`. Build target for Phase 1 is `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild, no TF-M). Phial hardware is described by a shared `boards/phial-common.dtsi` included by a one-line per-app overlay stub. The `app/led-test/` application drives 16 GPIO-backed LEDs via the Zephyr LED API and exposes a `phial` shell command surface alongside the standard Zephyr shell commands.

**Tech Stack:** Zephyr RTOS, nRF Connect SDK (NCS), Nordic nRF54L15-QFAA, west build system, devicetree, Kconfig, Zephyr LED API (gpio-leds), Zephyr shell subsystem.

---

## Prerequisites

The implementing engineer is assumed to have:

- A working nRF Connect SDK toolchain (`nrfutil`, west, Zephyr SDK at `ZEPHYR_SDK_INSTALL_DIR`, or NCS toolchain manager output on `PATH`). If not, follow Nordic's "nRF Connect SDK getting started" guide first.
- `west` ≥ 1.2.0, `cmake` ≥ 3.20, Python 3.8+.
- A Phial board (KiCad project `pilulith`) with the four center LEDs (LED13–16) populated.
- A J-Link probe (or nRF DK as in-line debugger) connected via SWD to the Phial board's debug header.
- A serial terminal (`picocom`, `minicom`, or `nrfutil device monitor`) capable of 115200 baud 8N1.

**Workspace layout** — after `west init -l .` and `west update`, the directory structure is:

```
~/golioth/                  (or wherever)
├── phial-fw/               ← this repo
│   ├── west.yml
│   ├── boards/
│   ├── conf/
│   ├── app/led-test/
│   └── ...
├── deps/                   ← created by west update; vendor code
│   ├── nrf/
│   ├── zephyr/
│   ├── modules/...
│   └── ...
└── modules/lib/golioth-firmware-sdk/
```

---

## File structure

Files this plan creates (no existing files to modify — this is a fresh repo):

```
phial-fw/
├── .checkpatch.conf            # RDT-derived; whitespace/style guard
├── .clang-format               # RDT-derived; clang-format rules
├── .editorconfig               # RDT-derived; tabs/spaces convention
├── .gitignore                  # ignores build/, .west/, deps/
├── CHANGELOG.md                # versioning log; starts at 0.1.0
├── LICENSE                     # Apache-2.0
├── README.md                   # build + flash instructions, phase status
├── VERSION                     # 0.1.0
├── west.yml                    # T2 manifest, imports golioth-firmware-sdk → NCS
├── boards/
│   └── phial-common.dtsi       # SHARED Phial hardware description
├── conf/
│   └── shell-common.conf       # SHARED Kconfig: shell + log + standard cmds
└── app/
    └── led-test/
        ├── CMakeLists.txt
        ├── prj.conf
        ├── sample.yaml
        ├── boards/
        │   └── nrf54l15dk_nrf54l15_cpuapp.overlay   # 1-line include of phial-common.dtsi
        └── src/
            ├── main.c                # thin bootstrap; spawn animation thread
            ├── led_anim.c            # animation engine, state machine, thread
            ├── led_anim.h
            └── shell_cmds.c          # `phial` subcommand tree
```

**Responsibility per file:**

- `boards/phial-common.dtsi` — single source of truth for Phial hardware. Declares all 16 LEDs as `gpio-leds`, structural placeholders for I²C/PDM/UART/buttons. Every app inherits via `#include`.
- `conf/shell-common.conf` — every app's shell baseline. App-specific Kconfig stays in the app's own `prj.conf`.
- `app/led-test/src/main.c` — *thin* bootstrap. Verifies the LED device is ready and launches the animation thread. No animation logic here.
- `app/led-test/src/led_anim.c` — animation state machine in its own thread. Patterns are pure functions of `(pattern, step_count)` → which LEDs to set. Public API: `led_anim_init()`, `led_anim_set_pattern()`, `led_anim_set_period()`, `led_anim_get_pattern()`, `led_anim_get_period()`.
- `app/led-test/src/shell_cmds.c` — `phial` Zephyr shell subcommand tree. Calls `led_anim_*` setters and direct `led_on/led_off` for per-LED control. No animation logic; pure adapter.

---

## Testing philosophy

Firmware bringup work has two test loops:

1. **Build check** — `west build` succeeds. Catches devicetree mistakes, missing symbols, Kconfig typos. Run after every code change. Fully automated.
2. **Hardware check** — flash the board, observe LEDs, type shell commands. Catches polarity errors, wrong pins, animation bugs, missing peripherals. Manual but unambiguous.

We do not write host-side unit tests for the animation state machine in this phase — the value/effort ratio is poor for a 30-line state machine and we can swap pattern implementations in 30 seconds with the shell. Phase 2 (when PWM lands and patterns get more complex) is a reasonable point to consider ZTest coverage.

Each task ends with a build check; tasks that change observable behavior also end with a hardware check.

---

## Task 1: Initialize git ignore + base files

**Files:**
- Create: `phial-fw/.gitignore`
- Create: `phial-fw/VERSION`
- Create: `phial-fw/LICENSE`
- Create: `phial-fw/CHANGELOG.md`
- Create: `phial-fw/README.md`

- [ ] **Step 1: Write `.gitignore`**

`phial-fw/.gitignore`:

```
# Zephyr / west build artifacts
build/
build-*/
.west/

# West workspace siblings (created by `west update`)
/deps/
/modules/
/nrf/
/nrfxlib/
/zephyr/
/bootloader/
/tools/

# Editor noise
.DS_Store
*.swp
.vscode/
.idea/

# Python
__pycache__/
*.pyc
```

- [ ] **Step 2: Write `VERSION`**

`phial-fw/VERSION`:

```
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
```

- [ ] **Step 3: Write `LICENSE`**

Use Apache-2.0 (matches Golioth conventions). Copy from https://www.apache.org/licenses/LICENSE-2.0.txt into `phial-fw/LICENSE` verbatim.

- [ ] **Step 4: Write `CHANGELOG.md`**

`phial-fw/CHANGELOG.md`:

```markdown
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Repo skeleton, west manifest, shared board overlay (`boards/phial-common.dtsi`),
  shared shell configuration (`conf/shell-common.conf`).
- `app/led-test/`: minimal LED bringup application for nRF54L15 with chase
  animation and `phial` shell subcommand tree.
```

- [ ] **Step 5: Write `README.md`**

`phial-fw/README.md`:

```markdown
# Phial Firmware

Firmware for the Phial board (KiCad project `pilulith`, Nordic nRF54L15-QFAA).
Multi-app repo following the [Golioth Reference Design
Template](https://github.com/golioth/reference-design-template) conventions,
adapted for multiple test/demo applications living side-by-side.

## Applications

| Path              | Purpose                                                    | Phase  |
|-------------------|------------------------------------------------------------|--------|
| `app/led-test/`   | Drive all 16 LEDs; exercise Zephyr shell + I²C + GPIO       | 1, 2   |

## Setup

This repo is a [T2 freestanding
workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html).
After cloning, initialize and update west to pull NCS as a sibling tree:

    cd phial-fw
    west init -l .
    west update                 # first time: ~2 GB download, ~5–10 min

## Build & flash — `app/led-test`

Phase 1 build target (secure-only, no bootloader):

    west build -p -b nrf54l15dk/nrf54l15/cpuapp app/led-test
    west flash

Open a serial console at 115200 8N1 on the board's VCOM port. You should see:

    *** Booting Zephyr OS build ... ***
    phial:~$

Try:

    phial:~$ device list
    phial:~$ phial pattern chase
    phial:~$ i2c scan i2c1
    phial:~$ kernel reboot warm

## Design docs

See `docs/superpowers/specs/` for design history and `docs/superpowers/plans/`
for implementation plans.
```

- [ ] **Step 6: Commit**

```bash
cd /home/chrisg/golioth/phial-fw
git add .gitignore VERSION LICENSE CHANGELOG.md README.md
git commit -m "Add repo base files: gitignore, VERSION, LICENSE, README, CHANGELOG"
```

---

## Task 2: Add RDT tooling files (formatter, style)

**Files:**
- Create: `.checkpatch.conf`
- Create: `.clang-format`
- Create: `.editorconfig`

These three files come verbatim from the Golioth Reference Design Template at https://github.com/golioth/reference-design-template — the formatter/style conventions are unchanged from RDT.

- [ ] **Step 1: Fetch and write `.checkpatch.conf`**

Run:

```bash
cd /home/chrisg/golioth/phial-fw
curl -sL https://raw.githubusercontent.com/golioth/reference-design-template/main/.checkpatch.conf -o .checkpatch.conf
```

- [ ] **Step 2: Fetch and write `.clang-format`**

```bash
curl -sL https://raw.githubusercontent.com/golioth/reference-design-template/main/.clang-format -o .clang-format
```

- [ ] **Step 3: Fetch and write `.editorconfig`**

```bash
curl -sL https://raw.githubusercontent.com/golioth/reference-design-template/main/.editorconfig -o .editorconfig
```

- [ ] **Step 4: Verify files have content**

Run:

```bash
wc -l .checkpatch.conf .clang-format .editorconfig
```

Expected: each line count > 0; none empty.

- [ ] **Step 5: Commit**

```bash
git add .checkpatch.conf .clang-format .editorconfig
git commit -m "Add RDT-derived style and formatter config files"
```

---

## Task 3: Write the west manifest

**Files:**
- Create: `west.yml`

- [ ] **Step 1: Write `west.yml`**

This is the RDT manifest with two changes: `self.path` set to `phial-fw` and we drop modules irrelevant to Phial (`libostentus`, `golioth-zephyr-boards`, etc. — easy to add back later if needed).

`phial-fw/west.yml`:

```yaml
# Copyright (c) 2022-2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

manifest:
  version: 1.0

  projects:
    - name: golioth
      path: modules/lib/golioth-firmware-sdk
      revision: v0.21.0
      url: https://github.com/golioth/golioth-firmware-sdk.git
      west-commands: scripts/west-commands.yml
      submodules: true
      import:
        file: west-ncs.yml
        path-prefix: deps
        name-allowlist:
          - nrf
          - zephyr
          - cmsis_6
          - hal_nordic
          - mbedtls
          - mbedtls-nrf
          - mcuboot
          - net-tools
          - nrfxlib
          - oberon-psa-crypto
          - qcbor
          - segger
          - tfm-mcuboot
          - tinycrypt
          - trusted-firmware-m
          - zcbor

  self:
    path: phial-fw
```

- [ ] **Step 2: Initialize the west workspace**

Run:

```bash
cd /home/chrisg/golioth/phial-fw
west init -l .
```

Expected output: `=== Initializing from existing manifest repository phial-fw` and creation of a `.west/config` file.

- [ ] **Step 3: Pull NCS and dependencies**

Run:

```bash
west update
```

Expected: long-running (~5–10 min first time, ~2 GB download). Creates sibling `deps/` directory containing `nrf/`, `zephyr/`, `modules/`, etc.

Note: if the engineer is on a constrained network, they can `west update --narrow --fetch-opt=--depth=1` for a shallow clone, but the full clone is recommended for the project's lifetime.

- [ ] **Step 4: Verify Zephyr base is discoverable**

Run:

```bash
ls ../deps/zephyr/CMakeLists.txt
```

Expected: file exists. (Path is `../deps/zephyr/...` because `self.path: phial-fw` puts this repo at workspace root, and `west update` placed Zephyr under `../deps/zephyr/`.)

- [ ] **Step 5: Commit the manifest**

```bash
git add west.yml
git commit -m "Add west manifest (T2 freestanding workspace via golioth-firmware-sdk)"
```

---

## Task 4: Write the shared shell-common Kconfig fragment

**Files:**
- Create: `conf/shell-common.conf`

- [ ] **Step 1: Create directory and write file**

`phial-fw/conf/shell-common.conf`:

```
# ----- Shell core -----
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="phial:~$ "
CONFIG_SHELL_HISTORY=y
CONFIG_SHELL_TAB=y
CONFIG_SHELL_TAB_AUTOCOMPLETION=y
CONFIG_SHELL_LOG_BACKEND=y

# ----- Built-in shell command modules -----
CONFIG_DEVICE_SHELL=y       # `device list`
CONFIG_KERNEL_SHELL=y       # `kernel uptime/reboot/threads/stacks`
CONFIG_GPIO_SHELL=y         # `gpio conf/get/set`
CONFIG_I2C_SHELL=y          # `i2c scan/recover/read/write`
CONFIG_LED_SHELL=y          # `led on/off/set_brightness`
CONFIG_HWINFO_SHELL=y       # `hwinfo devid` + reset cause
CONFIG_FLASH_SHELL=y        # `flash read/write/erase`
CONFIG_REGULATOR_SHELL=y    # nPM2100 control
CONFIG_SENSOR_SHELL=y       # `sensor get bme280` etc.
CONFIG_PWM_SHELL=y          # Phase 2 — once pwm-leds is wired
CONFIG_LOG_CMDS=y           # runtime log levels

# ----- Logging -----
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_BACKEND_UART=y

# ----- Console -----
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_PRINTK=y
```

- [ ] **Step 2: Commit**

```bash
git add conf/shell-common.conf
git commit -m "Add shared shell + log Kconfig fragment (conf/shell-common.conf)"
```

---

## Task 5: Write the shared board overlay (LEDs + aliases)

**Files:**
- Create: `boards/phial-common.dtsi`

The dtsi declares the LEDs in concrete form (we have the pin map). Other peripherals (I²C, PDM, buttons) are deliberately omitted from this commit so the first compile succeeds with minimal surface area; they get added in their own subsequent commit during Phase 1 or 2 as their pin details are confirmed.

- [ ] **Step 1: Write `boards/phial-common.dtsi`**

`phial-fw/boards/phial-common.dtsi`:

```dts
/*
 * Phial board hardware description — shared across every app in this repo.
 * Pulled in by each app's `boards/<board_target>.overlay` via #include.
 *
 * Source of truth for pin assignments: docs/superpowers/specs/2026-05-14-phial-bringup-design.md
 *
 * NOTE: LEDs 13–16 are on the TRACEDATA / SWO pins (P2.07–P2.10). They work
 * fine as plain GPIOs, but enabling SWO-based debug output will collide with
 * LED16 (P2.07). Standard SWD debug on the dedicated SWDIO/SWCLK pins is
 * unaffected.
 */

/ {
    leds: leds {
        compatible = "gpio-leds";

        /* Ring LEDs 1–12 — clock-face layout */
        led01: led_01 { gpios = <&gpio1 10 GPIO_ACTIVE_HIGH>; label = "LED01 (ring 1)";  };
        led02: led_02 { gpios = <&gpio1  9 GPIO_ACTIVE_HIGH>; label = "LED02 (ring 2)";  };
        led03: led_03 { gpios = <&gpio0  3 GPIO_ACTIVE_HIGH>; label = "LED03 (ring 3)";  };
        led04: led_04 { gpios = <&gpio0  2 GPIO_ACTIVE_HIGH>; label = "LED04 (ring 4)";  };
        led05: led_05 { gpios = <&gpio2  6 GPIO_ACTIVE_HIGH>; label = "LED05 (ring 5)";  };
        led06: led_06 { gpios = <&gpio2  5 GPIO_ACTIVE_HIGH>; label = "LED06 (ring 6)";  };
        led07: led_07 { gpios = <&gpio2  4 GPIO_ACTIVE_HIGH>; label = "LED07 (ring 7)";  };
        led08: led_08 { gpios = <&gpio2  3 GPIO_ACTIVE_HIGH>; label = "LED08 (ring 8)";  };
        led09: led_09 { gpios = <&gpio2  2 GPIO_ACTIVE_HIGH>; label = "LED09 (ring 9)";  };
        led10: led_10 { gpios = <&gpio2  1 GPIO_ACTIVE_HIGH>; label = "LED10 (ring 10)"; };
        led11: led_11 { gpios = <&gpio2  0 GPIO_ACTIVE_HIGH>; label = "LED11 (ring 11)"; };
        led12: led_12 { gpios = <&gpio1 14 GPIO_ACTIVE_HIGH>; label = "LED12 (ring 12)"; };

        /* Center LEDs 13–16 — on TRACEDATA / SWO pins */
        led13: led_13 { gpios = <&gpio2  8 GPIO_ACTIVE_HIGH>; label = "LED13 (center)"; };  /* TRACEDATA1 */
        led14: led_14 { gpios = <&gpio2  9 GPIO_ACTIVE_HIGH>; label = "LED14 (center)"; };  /* TRACEDATA2 */
        led15: led_15 { gpios = <&gpio2 10 GPIO_ACTIVE_HIGH>; label = "LED15 (center)"; };  /* TRACEDATA3 */
        led16: led_16 { gpios = <&gpio2  7 GPIO_ACTIVE_HIGH>; label = "LED16 (center, SWO)"; };  /* SWO */
    };

    aliases {
        led0  = &led01;
        led1  = &led02;
        led2  = &led03;
        led3  = &led04;
        led4  = &led05;
        led5  = &led06;
        led6  = &led07;
        led7  = &led08;
        led8  = &led09;
        led9  = &led10;
        led10 = &led11;
        led11 = &led12;
        led12 = &led13;
        led13 = &led14;
        led14 = &led15;
        led15 = &led16;
    };
};
```

- [ ] **Step 2: Commit**

```bash
git add boards/phial-common.dtsi
git commit -m "Add shared board overlay with 16-LED pin map (gpio-leds)"
```

This concludes Phase 0.

- [ ] **Step 3: Tag the phase**

```bash
git tag phase-0-bootstrap
```

---

## Task 6: Create the `app/led-test/` skeleton with a stub `main.c` and verify build

**Files:**
- Create: `app/led-test/CMakeLists.txt`
- Create: `app/led-test/prj.conf`
- Create: `app/led-test/sample.yaml`
- Create: `app/led-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay`
- Create: `app/led-test/src/main.c`

- [ ] **Step 1: Write `CMakeLists.txt`**

`phial-fw/app/led-test/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pull in the shared shell/log Kconfig before find_package(Zephyr).
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../../conf/shell-common.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(led_test)

target_sources(app PRIVATE
    src/main.c
)
```

(led_anim.c and shell_cmds.c get added to `target_sources` when they're created in later tasks.)

- [ ] **Step 2: Write `prj.conf`**

`phial-fw/app/led-test/prj.conf`:

```
# LED API + GPIO backend (Phase 1)
CONFIG_LED=y
CONFIG_LED_GPIO=y

# Subsystems used by the app
CONFIG_GPIO=y
CONFIG_I2C=y                    # not used by code yet, but the bus must be
                                # available so `i2c scan` in the shell works.

# Slightly larger main stack — animation thread + shell + log
CONFIG_MAIN_STACK_SIZE=2048

# (Phase 2 will add):   CONFIG_LED_PWM=y / CONFIG_PWM=y
# (Phase 3 BLE adds):   CONFIG_BT=y / CONFIG_BT_PERIPHERAL=y / CONFIG_BT_SHELL=y
# (Phase 4 Pouch adds): CONFIG_GOLIOTH_*=y / CONFIG_BOOTLOADER_MCUBOOT=y
```

- [ ] **Step 3: Write `sample.yaml` (for twister / CI later)**

`phial-fw/app/led-test/sample.yaml`:

```yaml
sample:
  name: Phial LED test
  description: Drive 16 GPIO LEDs with patterns; exercise Zephyr shell.

common:
  build_only: true
  tags:
    - phial
    - led
    - shell

tests:
  sample.phial.led_test:
    platform_allow:
      - nrf54l15dk/nrf54l15/cpuapp
    integration_platforms:
      - nrf54l15dk/nrf54l15/cpuapp
```

- [ ] **Step 4: Write the per-app board overlay stub**

`phial-fw/app/led-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay`:

```dts
/*
 * Per-app overlay wrapper: pull in the shared Phial hardware description.
 * Add app-specific tweaks below the include if ever needed; otherwise leave alone.
 */

#include "../../../boards/phial-common.dtsi"
```

Path math: from `app/led-test/boards/` → `..` = `app/led-test/` → `../..` = `app/` → `../../..` = repo root → `../../../boards/phial-common.dtsi` ✓ (3 ups, not 4 as informally written in the spec).

- [ ] **Step 5: Write stub `main.c`**

`phial-fw/app/led-test/src/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }

    LOG_INF("Phial LED test booted, %d LEDs available",
            DT_CHILD_NUM(DT_PATH(leds)));
    return 0;
}
```

- [ ] **Step 6: Build the app**

Run from the repo root:

```bash
cd /home/chrisg/golioth/phial-fw
west build -p always -b nrf54l15dk/nrf54l15/cpuapp app/led-test
```

Expected: build completes with no errors. Output ends with a "Memory region Used Size Region Size %age Used" table and `[xxx/xxx]  Linking C executable zephyr/zephyr.elf`.

If the build fails on `DEVICE_DT_GET_ANY(gpio_leds)` returning null because the devicetree doesn't yet have a `gpio-leds` node visible to this build, double-check the overlay include path resolved correctly (look at `build/zephyr/zephyr.dts` for an `leds` node).

- [ ] **Step 7: Commit**

```bash
git add app/led-test/CMakeLists.txt app/led-test/prj.conf app/led-test/sample.yaml \
        app/led-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay app/led-test/src/main.c
git commit -m "Add app/led-test skeleton: builds + boots, no animation yet"
```

---

## Task 7: First-light single-LED blink

Verify the board itself is alive and the pin map is correct by blinking *one* LED slowly. Smallest possible behavioral test on hardware.

**Files:**
- Modify: `app/led-test/src/main.c`

- [ ] **Step 1: Replace `main.c` with single-LED blinker**

`phial-fw/app/led-test/src/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BLINK_LED_IDX  0   /* led0 alias → LED01 on P1.10 */
#define BLINK_PERIOD_MS  500

int main(void)
{
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);

    if (leds == NULL || !device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }

    LOG_INF("Phial first-light: blinking LED %d every %d ms",
            BLINK_LED_IDX, BLINK_PERIOD_MS);

    bool on = false;
    while (1) {
        on = !on;
        if (on) {
            led_on(leds, BLINK_LED_IDX);
        } else {
            led_off(leds, BLINK_LED_IDX);
        }
        k_msleep(BLINK_PERIOD_MS);
    }

    return 0;
}
```

- [ ] **Step 2: Build**

```bash
west build -p always -b nrf54l15dk/nrf54l15/cpuapp app/led-test
```

Expected: clean build.

- [ ] **Step 3: Flash**

Connect the J-Link or attached debugger and run:

```bash
west flash
```

Expected: `flash` succeeds; the board resets and starts running.

- [ ] **Step 4: Hardware verification**

Open a serial console at 115200 8N1:

```bash
nrfutil device monitor --baudrate 115200      # or: picocom -b 115200 /dev/ttyACM0
```

Verify:

- The boot banner appears.
- The log line `Phial first-light: blinking LED 0 every 500 ms` appears.
- **LED01 (the first LED on the clock ring) blinks at ~1 Hz**.

If LED01 does not blink:
1. Check `i2c scan` is not failing (might mean the UART pinctrl isn't working either — bigger problem).
2. Open `build/zephyr/zephyr.dts` and confirm the `leds` node lists `led_01` with `gpios = <&gpio1 10 GPIO_ACTIVE_HIGH>`.
3. If LED01 is fully on or fully off (not blinking), polarity may be reversed: in `phial-common.dtsi` change `GPIO_ACTIVE_HIGH` → `GPIO_ACTIVE_LOW` for the affected LEDs and rebuild.
4. If a different LED blinks, the index→alias mapping in `phial-common.dtsi` is wrong.

- [ ] **Step 5: Commit**

```bash
git add app/led-test/src/main.c
git commit -m "First-light blinker: prove LED01 + pin map on hardware"
```

---

## Task 8: Add `led_anim` module with `LED_PATTERN_CHASE`

**Files:**
- Create: `app/led-test/src/led_anim.h`
- Create: `app/led-test/src/led_anim.c`
- Modify: `app/led-test/src/main.c`
- Modify: `app/led-test/CMakeLists.txt`

- [ ] **Step 1: Write `led_anim.h`**

`phial-fw/app/led-test/src/led_anim.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <stdint.h>

#define PHIAL_LED_COUNT  16

enum led_pattern {
    LED_PATTERN_OFF,
    LED_PATTERN_ALL_ON,
    LED_PATTERN_CHASE,
    LED_PATTERN_RING_SWEEP,
    LED_PATTERN_CENTER_PULSE,
    LED_PATTERN_CLOCK,
    LED_PATTERN_BREATHING,   /* Phase 2 only — requires pwm-leds */
};

/* Spawn the animation thread. Must be called once at startup. */
int led_anim_init(void);

void led_anim_set_pattern(enum led_pattern p);
void led_anim_set_period(uint32_t ms);

enum led_pattern led_anim_get_pattern(void);
uint32_t         led_anim_get_period(void);

#endif /* LED_ANIM_H */
```

- [ ] **Step 2: Write `led_anim.c` — chase only for now, other patterns added in Task 9**

`phial-fw/app/led-test/src/led_anim.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include "led_anim.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_anim, LOG_LEVEL_INF);

#define LED_ANIM_STACK_SIZE   1024
#define LED_ANIM_PRIORITY     7
#define DEFAULT_PERIOD_MS     100

static const struct device *leds_dev;
static enum led_pattern     current_pattern = LED_PATTERN_CHASE;
static uint32_t             current_period_ms = DEFAULT_PERIOD_MS;
static uint32_t             step;

static void all_off(void)
{
    for (int i = 0; i < PHIAL_LED_COUNT; i++) {
        led_off(leds_dev, i);
    }
}

static void step_chase(void)
{
    /* Single LED, advancing each tick, wrapping at PHIAL_LED_COUNT. */
    all_off();
    led_on(leds_dev, step % PHIAL_LED_COUNT);
}

static void anim_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        switch (current_pattern) {
        case LED_PATTERN_CHASE:
            step_chase();
            break;
        default:
            /* Other patterns added in Task 9 */
            all_off();
            break;
        }
        step++;
        k_msleep(current_period_ms);
    }
}

K_THREAD_STACK_DEFINE(anim_stack, LED_ANIM_STACK_SIZE);
static struct k_thread anim_thread_data;

int led_anim_init(void)
{
    leds_dev = DEVICE_DT_GET_ANY(gpio_leds);
    if (leds_dev == NULL || !device_is_ready(leds_dev)) {
        LOG_ERR("gpio-leds device not ready");
        return -ENODEV;
    }

    k_thread_create(&anim_thread_data, anim_stack, LED_ANIM_STACK_SIZE,
                    anim_thread, NULL, NULL, NULL,
                    LED_ANIM_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&anim_thread_data, "led_anim");
    return 0;
}

void led_anim_set_pattern(enum led_pattern p)
{
    current_pattern = p;
    step = 0;
}

void led_anim_set_period(uint32_t ms)
{
    if (ms == 0) {
        return;
    }
    current_period_ms = ms;
}

enum led_pattern led_anim_get_pattern(void) { return current_pattern; }
uint32_t         led_anim_get_period(void)  { return current_period_ms; }
```

- [ ] **Step 3: Replace `main.c` to use `led_anim_init()`**

`phial-fw/app/led-test/src/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include "led_anim.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err = led_anim_init();
    if (err) {
        LOG_ERR("led_anim_init failed: %d", err);
        return err;
    }

    LOG_INF("Phial LED test up, default pattern: chase");
    return 0;
}
```

- [ ] **Step 4: Update `CMakeLists.txt` to include `led_anim.c`**

`phial-fw/app/led-test/CMakeLists.txt` — replace the `target_sources` block:

```cmake
target_sources(app PRIVATE
    src/main.c
    src/led_anim.c
)
```

- [ ] **Step 5: Build**

```bash
west build -p always -b nrf54l15dk/nrf54l15/cpuapp app/led-test
```

Expected: clean build.

- [ ] **Step 6: Flash and verify**

```bash
west flash
```

Verify on hardware: all 16 LEDs cycle in sequence (LED01 → LED02 → ... → LED16 → wrap), each on for 100 ms. The "chase" should make a clean visible rotation around the clock ring (LED01–12) followed by the four center LEDs (LED13–16), then back to LED01.

If the chase visibly skips or stalls on a particular LED, the alias for that index is wrong in `phial-common.dtsi`.

- [ ] **Step 7: Commit**

```bash
git add app/led-test/src/led_anim.c app/led-test/src/led_anim.h \
        app/led-test/src/main.c app/led-test/CMakeLists.txt
git commit -m "Add led_anim thread with CHASE pattern (all 16 LEDs)"
```

---

## Task 9: Add remaining patterns (OFF, ALL_ON, RING_SWEEP, CENTER_PULSE, CLOCK)

**Files:**
- Modify: `app/led-test/src/led_anim.c`

`LED_PATTERN_BREATHING` is left as a no-op stub — it requires `pwm-leds` which lands in Phase 2.

- [ ] **Step 1: Replace the `anim_thread` switch and add pattern step functions**

`phial-fw/app/led-test/src/led_anim.c` — replace the section between `static void step_chase(void)` and the `K_THREAD_STACK_DEFINE` line with:

```c
static void step_chase(void)
{
    all_off();
    led_on(leds_dev, step % PHIAL_LED_COUNT);
}

static void step_all_on(void)
{
    for (int i = 0; i < PHIAL_LED_COUNT; i++) {
        led_on(leds_dev, i);
    }
}

static void step_ring_sweep(void)
{
    /* Only the 12 ring LEDs participate; centers stay off. */
    all_off();
    led_on(leds_dev, step % 12);
}

static void step_center_pulse(void)
{
    /* Only the 4 center LEDs participate; ring stays off. */
    all_off();
    led_on(leds_dev, 12 + (step % 4));   /* aliases led12..led15 = LED13..LED16 */
}

static void step_clock(void)
{
    /* Sweep fills the ring like a seconds hand, then clears on wrap. */
    uint32_t pos = step % (12 + 1);
    all_off();
    for (uint32_t i = 0; i < pos; i++) {
        led_on(leds_dev, i);
    }
}

static void anim_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        switch (current_pattern) {
        case LED_PATTERN_OFF:
            all_off();
            break;
        case LED_PATTERN_ALL_ON:
            step_all_on();
            break;
        case LED_PATTERN_CHASE:
            step_chase();
            break;
        case LED_PATTERN_RING_SWEEP:
            step_ring_sweep();
            break;
        case LED_PATTERN_CENTER_PULSE:
            step_center_pulse();
            break;
        case LED_PATTERN_CLOCK:
            step_clock();
            break;
        case LED_PATTERN_BREATHING:
            /* Phase 2 only — pwm-leds not available yet. */
            all_off();
            break;
        }
        step++;
        k_msleep(current_period_ms);
    }
}
```

- [ ] **Step 2: Build**

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp app/led-test
```

Expected: clean build (pristine not required since file list didn't change).

- [ ] **Step 3: Flash**

```bash
west flash
```

(LEDs are still in chase by default — full pattern switching gets exercised by shell commands in Task 10.)

- [ ] **Step 4: Commit**

```bash
git add app/led-test/src/led_anim.c
git commit -m "led_anim: add OFF/ALL_ON/RING_SWEEP/CENTER_PULSE/CLOCK patterns"
```

---

## Task 10: Add `phial` shell command surface

**Files:**
- Create: `app/led-test/src/shell_cmds.c`
- Modify: `app/led-test/CMakeLists.txt`

- [ ] **Step 1: Write `shell_cmds.c`**

`phial-fw/app/led-test/src/shell_cmds.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include "led_anim.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static const char *pattern_name(enum led_pattern p)
{
    switch (p) {
    case LED_PATTERN_OFF:          return "off";
    case LED_PATTERN_ALL_ON:       return "all";
    case LED_PATTERN_CHASE:        return "chase";
    case LED_PATTERN_RING_SWEEP:   return "ring";
    case LED_PATTERN_CENTER_PULSE: return "center";
    case LED_PATTERN_CLOCK:        return "clock";
    case LED_PATTERN_BREATHING:    return "breathe";
    }
    return "?";
}

static int cmd_pattern(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_print(sh, "usage: phial pattern <off|all|chase|ring|center|clock|breathe>");
        return -EINVAL;
    }

    enum led_pattern p;
    if      (!strcmp(argv[1], "off"))     p = LED_PATTERN_OFF;
    else if (!strcmp(argv[1], "all"))     p = LED_PATTERN_ALL_ON;
    else if (!strcmp(argv[1], "chase"))   p = LED_PATTERN_CHASE;
    else if (!strcmp(argv[1], "ring"))    p = LED_PATTERN_RING_SWEEP;
    else if (!strcmp(argv[1], "center"))  p = LED_PATTERN_CENTER_PULSE;
    else if (!strcmp(argv[1], "clock"))   p = LED_PATTERN_CLOCK;
    else if (!strcmp(argv[1], "breathe")) {
        shell_print(sh, "breathe pattern requires pwm-leds (Phase 2, not in this build)");
        return -ENOTSUP;
    } else {
        shell_print(sh, "unknown pattern: %s", argv[1]);
        return -EINVAL;
    }

    led_anim_set_pattern(p);
    shell_print(sh, "pattern -> %s", pattern_name(p));
    return 0;
}

static int cmd_period(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_print(sh, "usage: phial period <ms>");
        return -EINVAL;
    }
    uint32_t ms = (uint32_t)strtoul(argv[1], NULL, 10);
    if (ms == 0) {
        shell_print(sh, "period must be > 0");
        return -EINVAL;
    }
    led_anim_set_period(ms);
    shell_print(sh, "period -> %u ms", ms);
    return 0;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) {
        shell_print(sh, "usage: phial led <0-15> <on|off>");
        return -EINVAL;
    }
    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= PHIAL_LED_COUNT) {
        shell_print(sh, "LED index out of range (0-15)");
        return -EINVAL;
    }
    const struct device *leds = DEVICE_DT_GET_ANY(gpio_leds);
    if (leds == NULL || !device_is_ready(leds)) {
        shell_print(sh, "gpio-leds device not ready");
        return -ENODEV;
    }
    if (!strcmp(argv[2], "on")) {
        led_on(leds, idx);
    } else if (!strcmp(argv[2], "off")) {
        led_off(leds, idx);
    } else {
        shell_print(sh, "second arg must be 'on' or 'off'");
        return -EINVAL;
    }

    /*
     * Note: the animation thread is still running. Direct led on/off will be
     * overwritten on the next animation tick unless `phial pattern off` is
     * issued first.
     */
    return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "pattern : %s", pattern_name(led_anim_get_pattern()));
    shell_print(sh, "period  : %u ms", led_anim_get_period());
    shell_print(sh, "uptime  : %lld ms", k_uptime_get());
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(phial_subcmds,
    SHELL_CMD(pattern, NULL, "set animation pattern", cmd_pattern),
    SHELL_CMD(period,  NULL, "set animation tick period in ms", cmd_period),
    SHELL_CMD(led,     NULL, "set a single LED on/off (overridden by animation)", cmd_led),
    SHELL_CMD(status,  NULL, "show current animation state", cmd_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(phial, &phial_subcmds, "Phial board commands", NULL);
```

- [ ] **Step 2: Add `shell_cmds.c` to `CMakeLists.txt`**

`phial-fw/app/led-test/CMakeLists.txt` — `target_sources` becomes:

```cmake
target_sources(app PRIVATE
    src/main.c
    src/led_anim.c
    src/shell_cmds.c
)
```

- [ ] **Step 3: Build**

```bash
west build -p always -b nrf54l15dk/nrf54l15/cpuapp app/led-test
```

Expected: clean build.

- [ ] **Step 4: Flash and verify shell commands**

```bash
west flash
```

Open serial console at 115200 8N1. At the `phial:~$` prompt, exercise each command:

```
phial:~$ phial status
pattern : chase
period  : 100 ms
uptime  : <some ms>

phial:~$ phial pattern off          # all LEDs off
phial:~$ phial pattern all          # all LEDs on
phial:~$ phial pattern ring         # ring sweeps, center off
phial:~$ phial pattern center       # center 4 cycle, ring off
phial:~$ phial pattern clock        # ring fills like seconds hand
phial:~$ phial pattern chase        # back to chase
phial:~$ phial period 50            # faster chase
phial:~$ phial period 500           # slower chase
phial:~$ phial led 5 on             # turn on LED06 directly (gets overwritten on next tick)
phial:~$ phial pattern off
phial:~$ phial led 0 on             # now stays on
phial:~$ phial led 0 off
phial:~$ phial pattern breathe      # should print "Phase 2" message and return
```

Each command should print confirmation and the LEDs should behave as described.

- [ ] **Step 5: Commit**

```bash
git add app/led-test/src/shell_cmds.c app/led-test/CMakeLists.txt
git commit -m "Add 'phial' shell subcommand tree (pattern/period/led/status)"
```

---

## Task 11: Phase 1 acceptance — verify against spec §5.5 success criteria

**Files:** none modified.

This is the explicit Phase 1 exit gate. Each of the six items below must pass.

- [ ] **Step 1: Boot banner + shell prompt**

Flash, open console. Expect:
```
*** Booting Zephyr OS build ...***
[00:00:00.xxx,xxx] <inf> main: Phial LED test up, default pattern: chase
phial:~$
```
Result: PASS / FAIL.

- [ ] **Step 2: Chase pattern visible**

LEDs cycle LED01 → LED02 → … → LED16 → LED01, no skips, no stuck LEDs.
Result: PASS / FAIL.

- [ ] **Step 3: Per-LED control via `phial led`**

Stop the animation first, then step through each LED index manually (the Zephyr shell does not support `for` loops):

```
phial:~$ phial pattern off
phial:~$ phial led 0 on
phial:~$ phial led 0 off
phial:~$ phial led 1 on
phial:~$ phial led 1 off
... (through index 15)
```

Each LED activates exactly once when addressed by its index. The clock-ring sequence (indices 0-11 → LED01-LED12) and center sequence (indices 12-15 → LED13-LED16) must light the physically correct LED for each address — no miswiring.

Result: PASS / FAIL.

- [ ] **Step 4: Shell health (`kernel uptime`, `device list`)**

```
phial:~$ kernel uptime
phial:~$ device list
```
`kernel uptime` returns a millisecond value. `device list` lists the LED device (`leds`) and other registered bindings.
Result: PASS / FAIL.

- [ ] **Step 5: `gpio` and `i2c` shell commands respond**

```
phial:~$ gpio
phial:~$ i2c
```
Both print help or subcommand listings (commands are registered and responsive). Note: `i2c scan` may return 0 devices in Phase 1 because the I²C bus is not yet declared in `phial-common.dtsi` — that's expected. The point is the **command itself works**; the bus is populated in a follow-on commit.
Result: PASS / FAIL.

- [ ] **Step 6: `kernel reboot warm` reboots cleanly**

```
phial:~$ kernel reboot warm
```
Board resets; banner reappears; chase resumes.
Result: PASS / FAIL.

- [ ] **Step 7: Tag the phase if all pass**

```bash
git tag phase-1-minimal-led
```

If any item fails, file an issue (or note in `docs/superpowers/specs/`) describing the failure, debug, fix, and re-run this task.

---

## Task 12: Update README with Phase 1 status

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a "Phase status" section to the README**

Edit `phial-fw/README.md` and append before the "Design docs" section:

```markdown
## Phase status

| Phase | State    | Tag                    |
|-------|----------|------------------------|
| 0 — Bootstrap                  | ✅ done | `phase-0-bootstrap`     |
| 1 — Minimal LED bringup        | ✅ done | `phase-1-minimal-led`   |
| 2 — PWM + RDT-canonical scaffold | ⏳ next |                         |
| 3 — BLE bringup                | ⏳        |                         |
| 4 — Golioth Pouch port         | ⏳        |                         |

`git checkout phase-1-minimal-led` reproduces the minimal Phase 1 build.
```

- [ ] **Step 2: Update CHANGELOG.md**

Edit `phial-fw/CHANGELOG.md`, replace the `## [Unreleased]` heading with a real release:

```markdown
## [0.1.0] — 2026-05-14

### Added
- Repo skeleton: west manifest (T2 freestanding via golioth-firmware-sdk),
  shared board overlay (`boards/phial-common.dtsi`) with full 16-LED pin map,
  shared shell+log Kconfig (`conf/shell-common.conf`).
- `app/led-test/` Phase 1 minimal bringup: 16-LED GPIO chase animation,
  `phial pattern/period/led/status` shell commands, full Zephyr shell with
  `gpio`/`i2c`/`led`/`device`/`kernel`/`hwinfo`/`regulator`/`sensor`/`log`
  command modules enabled.
```

- [ ] **Step 3: Commit**

```bash
git add README.md CHANGELOG.md
git commit -m "Update README + CHANGELOG: Phase 1 minimal LED bringup done"
```

---

## Summary of artifacts

After this plan:

- **2 tags:** `phase-0-bootstrap`, `phase-1-minimal-led`. Either can be checked out and built.
- **1 application:** `app/led-test/` builds for `nrf54l15dk/nrf54l15/cpuapp` and drives all 16 LEDs.
- **1 shared board file:** `boards/phial-common.dtsi` — Phial pin map (LEDs declared concretely; other peripherals deferred until their pin specifics land).
- **1 shared Kconfig:** `conf/shell-common.conf` — every future app picks this up via one `list(APPEND EXTRA_CONF_FILE …)` line.
- **A repeatable build/flash workflow** documented in the README.

## What comes next (out of scope for this plan)

- Fill in I²C / PDM / UART / button pin specifics in `phial-common.dtsi` (own spec or own implementation plan).
- Wire BME280 / LIS2DH / ATECC608B / nPM2100 as I²C child nodes so `i2c scan` and `sensor get` return live data.
- Phase 2 — add sysbuild + MCUboot + TF-M; add `pwm-leds` for the breathing pattern.
- Phase 3 — `app/ble-test/`.
- Phase 4 — `app/pouch/`.

Each of the above is its own design + plan cycle.
