# Sensor-Pouch App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new `app/sensor-pouch` that acts as a pouch BLE-GATT device: the boot button triggers an advertise/sync that uploads the latest BME280 reading as a Golioth Stream datapoint, and a cloud Settings value `LED` (int 1-16) lights the matching board LED. A `sensorpouch status` shell command reports state.

**Architecture:** Two phases. **Phase 0 (Milestone 0)** upgrades the workspace to NCS v3.2.3 by restructuring the west manifest (pin `nrf` v3.2.3 directly, add the `pouch` module), then fixes/verifies all existing apps and the upstream pouch `ble_gatt` example build. **Phase 1** builds `app/sensor-pouch` by porting the example's BLE/credentials/OTA modules (from the now-vendored pouch tree) and adding Phial-specific modules (`env` ported from sensor-test, `led_index` pure+tested, `app_settings`, `main`). This app uses **sysbuild + MCUboot** (for OTA) — the only such app in the repo.

**Tech Stack:** Zephyr/NCS v3.2.3, nRF54L15, pouch (`github.com/golioth/pouch`) + its in-tree `golioth_sdk` (Stream + Settings + OTA), BLE GATT, PSA crypto (secp384r1), LittleFS + MCUmgr (cert provisioning), MCUboot/sysbuild.

**Design spec:** [docs/superpowers/specs/2026-06-15-sensor-pouch-design.md](../specs/2026-06-15-sensor-pouch-design.md)

---

## Conventions used throughout

- `west` is the repo's venv west: `/Users/chrisg/golioth/phial-fw/.venv/bin/west`. Workspace root `/Users/chrisg/golioth/phial-fw`; manifest/git root `phial-app`. All `git` runs from `phial-app`. Work on `master` (user-approved).
- **Build commands differ by app.** Existing apps: `--no-sysbuild`. **sensor-pouch: `--sysbuild`** (MCUboot).
  ```bash
  cd /Users/chrisg/golioth/phial-fw/phial-app
  /Users/chrisg/golioth/phial-fw/.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp --sysbuild app/sensor-pouch
  ```
- Pristine builds take minutes — use a Bash timeout of 600000 ms. A `west update` after the manifest change can take many minutes (large NCS fetch) — use 600000 ms and expect a big download.
- After Phase 0, pouch is vendored at `deps/modules/lib/pouch/`; its example sources are at `deps/modules/lib/pouch/examples/zephyr/ble_gatt/src/`. **Ported files are copied from there**, not re-fetched.

---

# PHASE 0 — NCS 3.2.3 upgrade (Milestone 0)

> Gate: Phase 1 starts only after every existing app **and** the upstream pouch `ble_gatt` example build green on NCS 3.2.3. If a 3.2.3 API change forces broad rework, STOP and report — that is the trigger to split Phase 0 into its own plan (per the spec).

## Task 0.1: Restructure the west manifest to NCS v3.2.3 + add pouch

**Files:**
- Modify: `phial-app/west.yml` (full restructure)

Current `west.yml` imports NCS indirectly via the `golioth` project's `west-ncs.yml` (→ nrf v3.1.1). No golioth-SDK release pins 3.2.3, so we pin `nrf` directly.

- [ ] **Step 1: Replace `phial-app/west.yml` with:**

```yaml
# Copyright (c) 2022-2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

manifest:
  version: 1.0

  projects:
    # NCS pinned directly at v3.2.3 (pouch's verified version). Imports the
    # Zephyr/NCS module set under deps/ via name-allowlist. This replaces the
    # previous indirect import through golioth-firmware-sdk's west-ncs.yml.
    - name: nrf
      path: deps/nrf
      revision: v3.2.3
      url: https://github.com/nrfconnect/sdk-nrf
      import:
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
          - littlefs        # NEW: needed for the credentials filesystem

    # Classic Golioth firmware SDK, pinned at the commit pouch verifies against.
    - name: golioth
      path: deps/modules/lib/golioth-firmware-sdk
      revision: d703b1f8805c7584a44dabc31bdf09164637d888
      url: https://github.com/golioth/golioth-firmware-sdk.git
      west-commands: scripts/west-commands.yml
      submodules: true

    # Pouch transport + its in-tree golioth_sdk (Stream/Settings/OTA).
    - name: pouch
      path: deps/modules/lib/pouch
      revision: main          # pin to a tag/SHA once the build is confirmed
      url: https://github.com/golioth/pouch.git
      submodules: true

  self:
    path: phial-fw
```

- [ ] **Step 2: Update the workspace**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
/Users/chrisg/golioth/phial-fw/.venv/bin/west update    # big fetch; timeout 600000
/Users/chrisg/golioth/phial-fw/.venv/bin/pip install -r ../deps/modules/lib/pouch/requirements.txt
```
Expected: `west list` shows `nrf v3.2.3`, `zephyr` at the NCS-3.2.3 revision, and `pouch` present at `deps/modules/lib/pouch`. If `west update` errors on a missing project in the allowlist, add it (diff against the old allowlist — do not drop anything previously present).

- [ ] **Step 3: Sanity check the tree**

```bash
/Users/chrisg/golioth/phial-fw/.venv/bin/west list | grep -E "nrf|zephyr|pouch|golioth"
ls deps/modules/lib/pouch/examples/zephyr/ble_gatt   # confirms pouch vendored
```

- [ ] **Step 4: Commit**

```bash
git add west.yml
git commit -m "build: upgrade workspace to NCS v3.2.3 + add pouch module

Pin nrf v3.2.3 directly (no golioth-SDK release pins 3.2.3), golioth at pouch's
verified commit, add pouch at main. Add littlefs to the allowlist.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
> NOTE: `west.yml` is tracked but `deps/` is typically not — only the manifest is committed. Do not `git add deps/`.

## Task 0.2: Rebuild + fix all existing apps on NCS 3.2.3

**Files:** as needed under `app/led-test`, `app/sensor-test`, `app/buzzer-test`, `app/mic-test` (and possibly `boards/phial-common.dtsi` / `conf/`).

- [ ] **Step 1: Build each existing app (`--no-sysbuild`), capture failures**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
for a in led-test sensor-test buzzer-test mic-test; do
  echo "===== $a ====="
  /Users/chrisg/golioth/phial-fw/.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/$a 2>&1 | tail -20
done
```

- [ ] **Step 2: Fix breakage minimally**

For each failure, apply the **smallest** change that restores the build (renamed Kconfig symbols, moved/renamed DT bindings, changed driver API signatures, deprecated macros). Use systematic-debugging: read the exact error, find the 3.2.3 equivalent, change one thing, rebuild. Do NOT refactor beyond what the upgrade requires. If a fix is non-obvious or broad, report it.

- [ ] **Step 3: Re-run the wav host unit test (mic-test regression)**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app/app/mic-test/tests/wav
cc -std=c11 -Wall -I ../../src test_wav.c ../../src/wav.c -o /tmp/wav_test && /tmp/wav_test
```
Expected: `0 failures`.

- [ ] **Step 4: Commit (only if changes were needed)**

```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
git add -A
git commit -m "fix: build existing apps on NCS 3.2.3

<list the specific adjustments per app, or 'no changes needed'>

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
If all four built unchanged, note that in the report and skip the commit.

## Task 0.3: Build the upstream pouch ble_gatt example (proves pouch+BLE+sysbuild)

**Files:** none (build-only verification).

- [ ] **Step 1: Build the vendored example for our board, with sysbuild**

```bash
cd /Users/chrisg/golioth/phial-fw
/Users/chrisg/golioth/phial-fw/.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp --sysbuild \
  deps/modules/lib/pouch/examples/zephyr/ble_gatt -d /tmp/pouch_ble_gatt_build 2>&1 | tail -30
```
Expected: a clean build producing MCUboot + app images. This proves pouch, the golioth_sdk, BLE, PSA, LittleFS, MCUmgr, and sysbuild/MCUboot all compile/link on our NCS 3.2.3 for the nRF54L15.

- [ ] **Step 2: Record the outcome**

Report: build pass/fail, flash/RAM summary, and any warnings of note (PSA/secp384r1, BLE controller). If it FAILS, STOP and report — this is the Milestone 0 gate; do not start Phase 1. Likely fixes if needed: pin pouch to a release tag instead of `main`, or a board-specific overlay tweak. No commit (out-of-tree build dir).

> **Milestone 0 done-criterion:** Tasks 0.1-0.3 all green. Only then proceed to Phase 1.

---

# PHASE 1 — the `app/sensor-pouch` feature

## File structure (Phase 1)

| File | Responsibility | Source |
|------|----------------|--------|
| `app/sensor-pouch/CMakeLists.txt` | Build config | New |
| `app/sensor-pouch/prj.conf` | App Kconfig | New (from example, trimmed) |
| `app/sensor-pouch/sysbuild.conf` | MCUboot on | New |
| `app/sensor-pouch/sample.yaml` | Twister metadata | New |
| `app/sensor-pouch/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Partitions + LFS + hardware | New |
| `app/sensor-pouch/src/ble_peripheral.{c,h}` | BLE GATT peripheral | Copy from vendored example, adapt |
| `app/sensor-pouch/src/credentials.{c,h}` | DER cert/key → PSA | Copy from vendored example |
| `app/sensor-pouch/src/fw_update.c`, `fatal_error.c` | OTA + fatal handler | Copy from vendored example |
| `app/sensor-pouch/src/env.{c,h}` | BME280 sample + cache | Port from `app/sensor-test/src/env.{c,h}` |
| `app/sensor-pouch/src/led_index.{c,h}` | Pure: setting → LED index | New (unit-tested) |
| `app/sensor-pouch/tests/led_index/test_led_index.c` | Host unit test | New |
| `app/sensor-pouch/src/app_settings.{c,h}` | `LED` settings handler → LED | New |
| `app/sensor-pouch/src/main.c` | pouch init, uplink, button, shell | New |
| `app/sensor-pouch/README.md` | Build/provision/usage | New |
| `README.md` (repo) | Add sensor-pouch row | Modify |

---

## Task 1: Scaffold sensor-pouch (builds with --sysbuild, BLE shell up)

**Files:** CMakeLists.txt, prj.conf, sysbuild.conf, sample.yaml, overlay, minimal main.c.

- [ ] **Step 1: `app/sensor-pouch/sysbuild.conf`**
```conf
SB_CONFIG_BOOTLOADER_MCUBOOT=y
```

- [ ] **Step 2: `app/sensor-pouch/boards/nrf54l15dk_nrf54l15_cpuapp.overlay`**

Includes the shared hardware, then **restores the stock MCUboot partition layout** (the shared dtsi shrank slot1 for mic-test) and mounts LittleFS for credentials.
```dts
/*
 * sensor-pouch overlay: shared Phial hardware + MCUboot-friendly partitions.
 *
 * phial-common.dtsi shrank slot1 to 536K and added mic_clip (for mic-test).
 * MCUboot image swap needs slot0 == slot1 (both 664K), so we restore the stock
 * slot1 size and delete mic_clip (unused here). The dtsi's "no MCUboot" comment
 * predates this app and is stale for sensor-pouch.
 */
#include "../../../boards/phial-common.dtsi"

/delete-node/ &mic_clip_partition;

&slot1_partition {
    reg = <0xb6000 DT_SIZE_K(664)>;   /* 0xb6000..0x15c000, matches slot0 size */
};

/ {
    fstab {
        compatible = "zephyr,fstab";
        lfs1: lfs1 {
            compatible = "zephyr,fstab,littlefs";
            mount-point = "/lfs1";
            partition = <&storage_partition>;
            automount;
            read-size = <16>;
            prog-size = <16>;
            cache-size = <64>;
            lookahead-size = <32>;
            block-cycles = <512>;
        };
    };
};
```
> Verify label names against `boards/phial-common.dtsi` (`mic_clip_partition`, `slot1_partition`) and the stock `nrf54l15_partition.dtsi` (`storage_partition`) before building.

- [ ] **Step 3: `app/sensor-pouch/prj.conf`** (from the example, trimmed; the boot button + LEDs + BME280 are Phial additions)
```conf
# BLE GATT pouch transport
CONFIG_BT=y
CONFIG_BT_SMP=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="Phial-Pouch"
CONFIG_BT_DEVICE_APPEARANCE=768
CONFIG_BT_BUF_ACL_TX_SIZE=251
CONFIG_BT_BUF_ACL_RX_SIZE=251
CONFIG_BT_L2CAP_TX_MTU=247
CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_RX_STACK_SIZE=4096
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_MBEDTLS_HEAP_SIZE=9192
CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED=n
CONFIG_MBEDTLS_PKCS5_C=n

CONFIG_LOG=y
CONFIG_ZCBOR=y
CONFIG_REBOOT=y
CONFIG_SHELL=y

# pouch + golioth-over-pouch
CONFIG_POUCH=y
CONFIG_POUCH_TRANSPORT_BLE_GATT=y
CONFIG_GOLIOTH=y
CONFIG_GOLIOTH_SETTINGS=y
CONFIG_GOLIOTH_OTA=y

# Credentials filesystem
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y
CONFIG_FILE_SYSTEM_SHELL=y

# MCUmgr (serial/SMP) for credential provisioning
CONFIG_MCUMGR=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_UART_CONSOLE_MCUMGR=y
CONFIG_BASE64=y
CONFIG_CRC=y
CONFIG_MCUMGR_TRANSPORT_SHELL=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_MCUMGR_GRP_OS_MCUMGR_PARAMS=y
CONFIG_MCUMGR_GRP_FS=y
CONFIG_MCUMGR_MGMT_NOTIFICATION_HOOKS=y
CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK=y

# OTA image handling
CONFIG_STREAM_FLASH=y
CONFIG_IMG_MANAGER=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y

# Phial hardware: BME280 + 16 LEDs
CONFIG_SENSOR=y
CONFIG_LED=y
CONFIG_LED_GPIO=y
CONFIG_GPIO=y
```
> This app does **not** use `conf/shell-common.conf` (it has its own shell/console config for MCUmgr passthrough). Do not add the shared shell conf.

- [ ] **Step 4: `app/sensor-pouch/CMakeLists.txt`**
```cmake
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_pouch)

target_sources(app PRIVATE
    src/main.c
)
```
(Sources are appended as each later task lands, like the mic-test plan.)

- [ ] **Step 5: `app/sensor-pouch/sample.yaml`**
```yaml
sample:
  name: Phial sensor-pouch
  description: BME280 -> Golioth Stream over pouch BLE GATT; cloud LED setting (1-16) lights an LED.
common:
  build_only: true
  tags:
    - phial
    - pouch
    - bluetooth
tests:
  sample.phial.sensor_pouch:
    platform_allow:
      - nrf54l15dk/nrf54l15/cpuapp
    integration_platforms:
      - nrf54l15dk/nrf54l15/cpuapp
    extra_args:
      - --sysbuild
```

- [ ] **Step 6: minimal `app/sensor-pouch/src/main.c`** (replaced in Task 6)
```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Phial sensor-pouch: scaffold");
    return 0;
}
```

- [ ] **Step 7: Build (`--sysbuild`) + commit**
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app
/Users/chrisg/golioth/phial-fw/.venv/bin/west build -p -b nrf54l15dk/nrf54l15/cpuapp --sysbuild app/sensor-pouch
```
Expected: clean MCUboot + app build. Then:
```bash
git add app/sensor-pouch/CMakeLists.txt app/sensor-pouch/prj.conf app/sensor-pouch/sysbuild.conf \
        app/sensor-pouch/sample.yaml app/sensor-pouch/boards app/sensor-pouch/src/main.c
git commit -m "feat(sensor-pouch): scaffold app (sysbuild/MCUboot, BLE+LFS+MCUmgr, partitions)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 2: Port BLE peripheral, credentials, OTA, fatal-error from the vendored example

**Files (copy from `deps/modules/lib/pouch/examples/zephyr/ble_gatt/src/`):**
- Create: `app/sensor-pouch/src/ble_peripheral.{c,h}`, `credentials.{c,h}`, `fw_update.c`, `fatal_error.c`
- Modify: `CMakeLists.txt` (add the four `.c` files), `prj.conf` if a referenced Kconfig is missing.

- [ ] **Step 1: Copy the files verbatim from the vendored example**
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app/app/sensor-pouch/src
SRC=../../../../deps/modules/lib/pouch/examples/zephyr/ble_gatt/src
cp $SRC/ble_peripheral.c $SRC/ble_peripheral.h $SRC/credentials.c $SRC/credentials.h \
   $SRC/fw_update.c $SRC/fatal_error.c .
```

- [ ] **Step 2: Reconcile dependencies.** Read each copied file. The example references `CONFIG_EXAMPLE_*` Kconfig (credentials dir, FW component, sync period) defined in the example's `Kconfig`. Add the ones actually used to `app/sensor-pouch/Kconfig` (create it, `source "$ZEPHYR_BASE/Kconfig"` + the needed `config EXAMPLE_*` entries copied from `deps/modules/lib/pouch/examples/zephyr/ble_gatt/Kconfig`), OR replace the few references with literal values. Prefer creating the `Kconfig` with the needed entries — smallest diff to the ported code. Keep `EXAMPLE_CREDENTIALS_DIR` = `/lfs1/credentials`.

- [ ] **Step 3: Add the four sources to `CMakeLists.txt`**
```cmake
target_sources(app PRIVATE
    src/main.c
    src/ble_peripheral.c
    src/credentials.c
    src/fw_update.c
    src/fatal_error.c
)
```

- [ ] **Step 4: Build (`--sysbuild`).** Fix only what's needed to compile (missing Kconfig, header paths). Report any non-trivial adaptation. Expected: clean build (main.c is still the stub; the ported modules just compile in).

- [ ] **Step 5: Commit**
```bash
git add app/sensor-pouch/src/ble_peripheral.* app/sensor-pouch/src/credentials.* \
        app/sensor-pouch/src/fw_update.c app/sensor-pouch/src/fatal_error.c \
        app/sensor-pouch/CMakeLists.txt app/sensor-pouch/Kconfig
git commit -m "feat(sensor-pouch): port BLE peripheral, credentials, OTA from pouch example

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 3: `led_index` — pure setting→LED-index decoder (TDD)

**Files:** Create `src/led_index.{c,h}`, `tests/led_index/test_led_index.c`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Write the failing test — `tests/led_index/test_led_index.c`**
```c
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-native unit test for the pure LED-index decoder.
 * Build + run (from this dir):
 *   cc -std=c11 -Wall -I ../../src test_led_index.c ../../src/led_index.c -o /tmp/li_test && /tmp/li_test
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "led_index.h"

static int checks, failures;
static void expect(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("FAIL: %s\n", what); }
}

int main(void)
{
    uint8_t idx = 0xAA;

    /* valid 1..16 -> 0..15 */
    expect(led_index_decode(1, &idx) && idx == 0,  "1 -> 0");
    expect(led_index_decode(16, &idx) && idx == 15, "16 -> 15");
    expect(led_index_decode(7, &idx) && idx == 6,  "7 -> 6");

    /* out of range -> false, idx untouched */
    idx = 42;
    expect(!led_index_decode(0, &idx) && idx == 42,  "0 rejected");
    expect(!led_index_decode(17, &idx) && idx == 42, "17 rejected");
    expect(!led_index_decode(-1, &idx) && idx == 42, "-1 rejected");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Create `src/led_index.h`**
```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_LED_INDEX_H_
#define PHIAL_LED_INDEX_H_

#include <stdbool.h>
#include <stdint.h>

#define NUM_LEDS 16

/* Decode a cloud "LED" setting value to a 0-based LED index. Returns true and
 * sets *out_idx for values 1..16; returns false (leaving *out_idx untouched)
 * for anything else. */
bool led_index_decode(int32_t setting_value, uint8_t *out_idx);

#endif /* PHIAL_LED_INDEX_H_ */
```

- [ ] **Step 3: Run the test, verify it FAILS** (no `led_index.c` yet)
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app/app/sensor-pouch/tests/led_index
cc -std=c11 -Wall -I ../../src test_led_index.c ../../src/led_index.c -o /tmp/li_test && /tmp/li_test
```
Expected: compile/link error (no `led_index.c`).

- [ ] **Step 4: Implement `src/led_index.c`**
```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "led_index.h"

bool led_index_decode(int32_t setting_value, uint8_t *out_idx)
{
    if (setting_value < 1 || setting_value > NUM_LEDS) {
        return false;
    }
    *out_idx = (uint8_t)(setting_value - 1);
    return true;
}
```

- [ ] **Step 5: Run the test, verify it PASSES** (`0 failures`).

- [ ] **Step 6: Add `src/led_index.c` to `CMakeLists.txt`; build (`--sysbuild`) to confirm it compiles in-tree.**

- [ ] **Step 7: Commit**
```bash
git add app/sensor-pouch/src/led_index.* app/sensor-pouch/tests/led_index/test_led_index.c app/sensor-pouch/CMakeLists.txt
git commit -m "feat(sensor-pouch): pure LED-index decoder (1-16) + host test

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 4: Port the `env` BME280 module from sensor-test

**Files:** Create `src/env.{c,h}`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Copy `env.h` and `env.c` verbatim** from `app/sensor-test/src/`.
```bash
cd /Users/chrisg/golioth/phial-fw/phial-app/app/sensor-pouch/src
cp ../../sensor-test/src/env.h ../../sensor-test/src/env.c .
```

- [ ] **Step 2: Remove the `env` shell command** from the copied `env.c` (the bottom `cmd_env` function + its `SHELL_CMD_REGISTER(env, ...)`), and drop the now-unused `#include <zephyr/shell/shell.h>`. Rationale: sensor-pouch's only shell command is `sensorpouch status` (spec); the cached reading is surfaced there. The sampling thread + `env_get()` are unchanged.

- [ ] **Step 3: Add `src/env.c` to `CMakeLists.txt`; build (`--sysbuild`).** The BME280 node comes from the shared dtsi (included by the overlay); `CONFIG_SENSOR=y` is set. Expected: clean build.

- [ ] **Step 4: Commit**
```bash
git add app/sensor-pouch/src/env.* app/sensor-pouch/CMakeLists.txt
git commit -m "feat(sensor-pouch): port BME280 env sampling module (no shell cmd)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 5: `app_settings` — the `LED` settings handler → light an LED

**Files:** Create `src/app_settings.{c,h}`; Modify `CMakeLists.txt`.

- [ ] **Step 1: `src/app_settings.h`**
```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_APP_SETTINGS_H_
#define PHIAL_APP_SETTINGS_H_

#include <zephyr/device.h>

/* Give the settings module the LED device to drive. Call once at startup. */
void app_settings_init(const struct device *leds);

/* Current 1-based LED index (1..16), or 0 if none set yet. For `status`. */
int app_settings_led_index(void);

#endif /* PHIAL_APP_SETTINGS_H_ */
```

- [ ] **Step 2: `src/app_settings.c`**
```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include <pouch/golioth/settings_callbacks.h>

#include "app_settings.h"
#include "led_index.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

static const struct device *g_leds;
static int g_index;   /* 1-based; 0 = unset */

void app_settings_init(const struct device *leds)
{
    g_leds = leds;
}

int app_settings_led_index(void)
{
    return g_index;
}

/* Light LED `idx0` (0-based) and turn all others off. */
static void show_only(uint8_t idx0)
{
    if (g_leds == NULL) {
        return;
    }
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i == idx0) {
            led_on(g_leds, i);
        } else {
            led_off(g_leds, i);
        }
    }
}

/* Golioth "LED" setting (int). 1..16 lights that LED; out-of-range is logged
 * and ignored (prior state kept). */
static int led_setting_cb(int32_t new_value)
{
    uint8_t idx0;

    if (!led_index_decode(new_value, &idx0)) {
        LOG_WRN("LED setting %d out of range 1..%d; ignoring", (int)new_value, NUM_LEDS);
        return 0;   /* accept the delivery; just don't act on a bad value */
    }
    LOG_INF("LED setting -> %d", (int)new_value);
    show_only(idx0);
    g_index = (int)new_value;
    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);
```
> Note: the handler name `LED` must match the Settings key configured in the Golioth console. The `int32_t` callback signature makes the `GOLIOTH_SETTINGS_HANDLER` `_Generic` register it as an INT setting.

- [ ] **Step 3: Add `src/app_settings.c` to `CMakeLists.txt`; build (`--sysbuild`).**

- [ ] **Step 4: Commit**
```bash
git add app/sensor-pouch/src/app_settings.* app/sensor-pouch/CMakeLists.txt
git commit -m "feat(sensor-pouch): LED settings handler (int 1-16) lights one LED

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 6: `main.c` — pouch init, uplink handler, button→sync, status shell

**Files:** Modify (replace) `src/main.c`.

Replaces the Task 1 stub. Wires everything: mounts are automatic (overlay `automount`), credentials load + `pouch_init`, register the uplink handler, start `env`, init LEDs + app_settings, init BLE, boot button (P1.12) interrupt → request a gateway, clear it on a pouch session-end event, and the `sensorpouch status` shell command.

- [ ] **Step 1: Replace `src/main.c`** (adapt the example's main.c — same pouch/BLE setup, plus our button-triggered-sync, BME280 uplink, multi-LED, and shell):
```c
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

/* Boot button = "request a gateway / sync now" (P1.12, from the shared dtsi
 * zephyr,user node). */
static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_gpios);
static struct gpio_callback button_cb;

static const struct device *g_leds;
static atomic_t g_syncing;          /* 1 while a gateway sync is in progress */
static const char *g_last_sync = "none";

/* Upload the latest BME280 reading as a Golioth Stream entry, on each sync. */
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

/* Clear the gateway-request flag once a session ends, returning to idle. */
static void on_pouch_event(enum pouch_event event)
{
    if (event == POUCH_EVENT_SESSION_END) {
        ble_peripheral_request_gateway(false);
        atomic_set(&g_syncing, 0);
        g_last_sync = "ok";
        LOG_INF("sync complete");
    }
}
POUCH_EVENT_HANDLER(on_pouch_event);

static void start_sync(void)
{
    if (atomic_set(&g_syncing, 1) == 1) {
        return;   /* already syncing */
    }
    g_last_sync = "in-progress";
    LOG_INF("sync requested");
    ble_peripheral_request_gateway(true);
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    start_sync();
}

static int setup_pouch(void)
{
    struct pouch_config config = {0};
    int err = load_certificate(&config.certificate);

    if (err) {
        LOG_ERR("No certificate (%d) — provision creds over MCUmgr, then reboot", err);
        return err;
    }
    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL) {
        LOG_ERR("No private key — provision creds over MCUmgr, then reboot");
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
        gpio_pin_configure_dt(&button, GPIO_INPUT) != 0 ||
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE) != 0) {
        LOG_WRN("boot button unavailable; use the gateway's own trigger");
        return;
    }
    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);
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

    bool pouch_ok = (setup_pouch() == 0);   /* stay up even without creds */

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

    setup_button();

    err = ble_peripheral_start();
    if (err) {
        LOG_ERR("BLE advertising start failed (%d)", err);
        return err;
    }
    /* Idle: do NOT auto-request a gateway. The boot button (or `sensorpouch
     * sync`, if added later) triggers a sync. */
    LOG_INF("ready%s — press the boot button to sync",
            pouch_ok ? "" : " (no creds: provision over MCUmgr + reboot)");
    return 0;
}

/* ---- sensorpouch shell ------------------------------------------------- */

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
```

> Implementer reconciliation notes (verify against the vendored pouch headers; the API may have shifted on `main`):
> - **Event API:** confirm `pouch/events.h` exposes `POUCH_EVENT_HANDLER` + an enum with a "session ended" member. The names above (`POUCH_EVENT_SESSION_END`, `POUCH_EVENT_HANDLER`) are best-guess — grep `deps/modules/lib/pouch/include/pouch/events.h` and adjust. If there is no session-end event, fall back to clearing the gateway-request flag from the uplink handler (after the write) or on a short timer.
> - **Content type / timeout constants:** confirm `POUCH_CONTENT_TYPE_JSON` and `POUCH_FOREVER` (grep `deps/modules/lib/pouch/include/pouch/`); the example `main.c` uses both.
> - **`ble_peripheral_*`:** signatures come from the copied `ble_peripheral.h`.
> - The example calls `ble_peripheral_request_gateway(true)` at startup ("request right away"); we deliberately do **not** (button-triggered model per the spec).

- [ ] **Step 2: Build (`--sysbuild`).** Reconcile any event/constant name mismatches against the vendored pouch headers (see notes). Expected: clean build. Report any API the plan guessed wrong.

- [ ] **Step 3: Commit**
```bash
git add app/sensor-pouch/src/main.c
git commit -m "feat(sensor-pouch): pouch init, BME280 uplink, button-triggered sync, status shell

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 7: Documentation — app README + repo table

**Files:** Create `app/sensor-pouch/README.md`; Modify repo `README.md`.

- [ ] **Step 1: `app/sensor-pouch/README.md`** covering: what it does; **`--sysbuild` build command** (note it's the one MCUboot app); credential provisioning via MCUmgr (`mcumgr/smpmgr fs upload crt.der/key.der → /lfs1/credentials/`, then reboot — copy the exact commands from the design spec / example README); the Golioth **`LED`** Settings key (int 1-16) and the `.s/sensor` Stream path; the boot-button-to-sync interaction and the need for a pouch **gateway**; and `sensorpouch status`.

- [ ] **Step 2: Add a row to the repo `README.md` Applications table** (read it first to match the current format):
```markdown
| `app/sensor-pouch/` | BME280 → Golioth Stream over **pouch** (BLE GATT); cloud `LED` setting (1-16) lights an LED. Built with `--sysbuild` (MCUboot). |
```

- [ ] **Step 3: Commit**
```bash
git add app/sensor-pouch/README.md README.md
git commit -m "docs(sensor-pouch): app README + repo Applications row

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## Task 8: Final verification (build + on-hardware — user-driven)

**Files:** none.

- [ ] **Step 1: Clean `--sysbuild` build** of `app/sensor-pouch`; note flash/RAM. 
- [ ] **Step 2: Re-run the `led_index` host test** (regression): `0 failures`.
- [ ] **Step 3: On-hardware (user):**
  1. Flash (sysbuild → MCUboot + app). First boot formats LittleFS (expected warning).
  2. Provision creds over MCUmgr: upload `crt.der` + `key.der` to `/lfs1/credentials/`, reboot.
  3. With the pouch **gateway** running: press the boot button → device advertises + requests a gateway → a BME280 datapoint appears in Golioth (Stream), and the `LED` setting is delivered.
  4. In the Golioth console, set `LED` to a value 1-16 → the matching board LED lights (others off); out-of-range is ignored.
  5. `sensorpouch status` → reflects sync state, LED index, latest reading.

> If pairing/connection fails, recall the example uses Just-Works (`CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE`); if PSA/secp384r1 errors appear at `pouch_init`, that's the CRACEN bring-up item flagged in the spec.

---

## Acceptance

- **Milestone 0:** all existing apps + the upstream pouch `ble_gatt` example build on NCS 3.2.3.
- `app/sensor-pouch` builds clean with `--sysbuild`; `led_index` host test passes.
- On hardware (with creds + gateway): button press streams a BME280 datapoint to Golioth; the `LED` setting (1-16) lights the matching LED; `sensorpouch status` works.
- App README + repo table updated; `phial-common.dtsi` and other apps unchanged except any NCS-3.2.3 build fixes from Milestone 0.
