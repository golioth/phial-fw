# Phial Board Bringup — Design

**Date:** 2026-05-14
**Scope:** Repo skeleton, shared board definition, first application (`led-test`), shared shell scaffolding, phase ladder through BLE and Golioth Pouch.
**Status:** Design, awaiting review before implementation plan.

---

## 1. Goals & non-goals

**Goals**

- Establish a Phial firmware repo that follows Golioth Reference Design Template (RDT) conventions, adapted for **multiple test/demo applications living side-by-side** rather than one application per repo.
- Bring up the Phial board (KiCad project: `pilulith`) on the nRF54L15-QFAA — first proof being all 16 onboard LEDs visibly driven through an animation.
- Make every application in the repo **shell-rich by default** (`gpio`, `i2c`, `led`, `sensor`, `regulator`, `kernel`, `device`, etc.) — the 1.5 MB flash budget makes this essentially free.
- Define a Zephyr devicetree representation of every Phial peripheral (LEDs, I²C sensors, buttons, PDM microphone, PMIC) once, in one shared overlay, even if individual apps only exercise a subset.
- Sequence the work so each phase has an unambiguous success criterion and **the first phase removes every variable that isn't the board file itself**.

**Non-goals (for this spec)**

- Final pin-channel mapping for hardware PWM (deferred to Phase 2).
- ATECC608B integration (deferred — not required by Pouch as currently planned).
- nPM2100 fuel-gauge / battery monitor logic (orthogonal to bringup; deferred).
- CI pipeline (set up after Phase 1 works locally; out of scope here).
- Audio capture from the PDM microphone (out of scope; covered by future audio sub-project if needed).

---

## 2. Hardware summary

Source: `~/golioth/phial-hw` (KiCad project `pilulith`).

| Ref | Part | Bus / Interface | Purpose |
|---|---|---|---|
| U1 | nRF54L15-QFAA-R | — | Main MCU (1.5 MB flash) |
| U2 | nPM2100-CAAA-R | I²C | Nordic primary-cell PMIC (coin cell) |
| U10 | LIS2DH | I²C | 3-axis accelerometer |
| U103 | ATECC608B-MAHDA | I²C | Microchip secure element |
| U105 | BME280 | I²C | Temperature / humidity / pressure |
| U106 | MP34DT05TR-A | PDM | Digital MEMS microphone |
| U102 | WLA-04 | — | Antenna |
| U104 | LF21115TMR | — | Load switch / LDO (to confirm) |
| BT101 | CR-2032 | — | Coin cell holder |
| SW1–SW4 | EVP-AWED4A | GPIO | Four tactile buttons |
| Y101 / Y102 | Crystals | — | 32 MHz HF + 32 kHz LF (to confirm) |
| D101–D112 | LEDs | GPIO | "Clock ring" LEDs 1–12 around board edge |
| LED13–LED16 | LEDs | GPIO | Four center LEDs (on TRACEDATA/SWO pins) |

### 2.1 LED pin map (source of truth)

| LED ref | net name  | MCU pin | nRF54L15 port              |
|---------|-----------|---------|----------------------------|
| D101    | LED01     | 38      | P1.10                      |
| D102    | LED02     | 37      | P1.09                      |
| D103    | LED03     | 28      | P0.03                      |
| D104    | LED04     | 27      | P0.02                      |
| D105    | LED05     | 17      | P2.06                      |
| D106    | LED06     | 16      | P2.05                      |
| D107    | LED07     | 15      | P2.04                      |
| D108    | LED08     | 14      | P2.03                      |
| D109    | LED09     | 13      | P2.02                      |
| D110    | LED10     | 12      | P2.01                      |
| D111    | LED11     | 11      | P2.00                      |
| D112    | LED12     | 42      | P1.14                      |
| LED13   | —         | 19      | P2.08 (TRACEDATA1 / T1)    |
| LED14   | —         | 20      | P2.09 (TRACEDATA2 / T2)    |
| LED15   | —         | 21      | P2.10 (TRACEDATA3 / T3)    |
| LED16   | —         | 18      | P2.07 (TRACEDATA0 / SWO)   |

LED01–LED12 form the clock-face ring; LED13–LED16 are the center cluster.

### 2.2 Hardware caveats

- **SWO / TRACEDATA conflict (LEDs 13–16).** P2.07–P2.10 are the SWO and TRACEDATA0–3 pins by default. They are usable as plain GPIOs and that is how we drive them here, but **enabling SWO-based debug output will collide with LED16 (P2.07)**. Standard SWD debug (SWDIO + SWCLK on dedicated debug pins) is unaffected. Document this in the README and in code comments at the LED13–16 nodes.
- **LED polarity is assumed active-high** in this spec. Schematic spot-check during implementation; flip in `phial-common.dtsi` if wrong.
- **PDM pin routing** — `pdm20`/`pdm21` are flexible via `NRF_PSEL` for any GPIO in the peripheral power domain (P1.x and P2.x verified; P0 has corner cases). The actual MIC_CLK / MIC_DATA pins in the schematic should be cross-checked against the nRF54L15 datasheet pin-routing table during Phase 2 (when PDM/pinctrl actually lands). This is a verify-during-implementation item, not a board-respin item — pin select is reroutable in software.
- **Peripheral budget** — nRF54L15 uses configurable SERIAL instances (UART / SPI / I²C). Phase 1 needs one SERIAL for shell UART and (eventually) one SERIAL configured as TWIM for I²C. PDM lives in its own dedicated peripheral and does not consume a SERIAL slot. The chip has plenty of SERIAL headroom for future expansion.

---

## 3. Repo skeleton

```
phial-fw/
├── .checkpatch.conf, .clang-format, .editorconfig, .gitignore   (copied from RDT)
├── CHANGELOG.md, LICENSE, README.md, VERSION
├── west.yml                                # T2 manifest, imports golioth-firmware-sdk → NCS
├── boards/
│   └── phial-common.dtsi                   # SHARED Phial pin map (LEDs, I²C, mic, buttons, PMIC)
├── conf/
│   └── shell-common.conf                   # SHARED Kconfig for shell + log + standard commands
├── app/
│   └── led-test/                           # first app (Phase 1)
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── sample.yaml
│       ├── boards/
│       │   └── nrf54l15dk_nrf54l15_cpuapp.overlay     # 1-line stub: #include "../../../../boards/phial-common.dtsi"
│       └── src/
│           ├── main.c
│           ├── led_anim.c / .h
│           └── shell_cmds.c
├── docs/superpowers/specs/                 # this design + future specs
└── pipelines/                              # CI (added after Phase 1)
```

**Key choices baked in:**

- **No custom Zephyr board file** and **no entry in `golioth-zephyr-boards`** for now. Build target is `nrf54l15dk/nrf54l15/cpuapp` (Phase 1) or `nrf54l15dk/nrf54l15/cpuapp/ns` (Phase 2+); the per-app overlay swaps in Phial pins. If/when a proper board file is wanted later, the canonical name is **`phial`**, with `phial_a` reserved for the first revision identifier.
- **`boards/phial-common.dtsi` lives at repo root** and is referenced via a one-line `#include` from each app's `boards/<board>.overlay`. No content is duplicated per app; the stub exists only because Zephyr's overlay auto-pickup looks under `<app_root>/boards/`.
- **`conf/shell-common.conf` lives at repo root** and is pulled into each app via `list(APPEND EXTRA_CONF_FILE …)` in the app's `CMakeLists.txt`.
- **`west.yml` cloned from RDT** but with `self.path: phial-fw` (named for this repo's role, not generic "app") since `self.path: app` no longer makes sense in a multi-app layout.
- **`VERSION` and `CHANGELOG.md` are repo-wide**, not per-app. Per-app versioning is overkill for the planned scope.

---

## 4. Shared board overlay — `boards/phial-common.dtsi`

The shared overlay is the **single source of truth** for Phial hardware. Every app inherits it via its stub overlay.

### 4.1 LEDs (concrete)

```dts
/ {
    leds: leds {
        compatible = "gpio-leds";
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
        led13: led_13 { gpios = <&gpio2  8 GPIO_ACTIVE_HIGH>; label = "LED13 (center)";  };  /* TRACEDATA1 */
        led14: led_14 { gpios = <&gpio2  9 GPIO_ACTIVE_HIGH>; label = "LED14 (center)";  };  /* TRACEDATA2 */
        led15: led_15 { gpios = <&gpio2 10 GPIO_ACTIVE_HIGH>; label = "LED15 (center)";  };  /* TRACEDATA3 */
        led16: led_16 { gpios = <&gpio2  7 GPIO_ACTIVE_HIGH>; label = "LED16 (center, SWO)"; }; /* SWO */
    };

    aliases {
        led0  = &led01;   /* 0-indexed; matches Zephyr LED API + `led` shell conventions */
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

Phase 2 adds a sibling `pwmleds` node for the subset of LED pins that land on PWM-capable channels (`pwm20`, `pwm21`, `pwm22` — up to 12 hardware channels). Specific pin↔channel mapping is decided at Phase 2 start; until then, all 16 LEDs are gpio-leds.

### 4.2 Other peripherals (structure now, pin specifics during implementation)

```dts
&uart??         { /* console for shell — pin assignment from schematic */ };

&i2c?? {
    status = "okay";
    pinctrl-0 = <&i2c??_default>;
    pinctrl-1 = <&i2c??_sleep>;
    pinctrl-names = "default", "sleep";

    nbme280: bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;  /* or 0x77 — confirm with schematic */
    };
    nlis2dh: lis2dh@?? {
        compatible = "st,lis2dh";
        reg = <0x??>;
    };
    natecc608: atecc608b@?? {
        compatible = "microchip,atecc608";
        reg = <0x??>;
    };
    /* nPM2100 — binding name depends on NCS revision pulled */
};

&pdm?? {
    status = "okay";
    pinctrl-0 = <&pdm??_default>;
    pinctrl-1 = <&pdm??_sleep>;
    pinctrl-names = "default", "sleep";
    clock-source = "PCLK32M";
};

/ {
    buttons: buttons {
        compatible = "gpio-keys";
        sw1: switch_1 { gpios = <...>; label = "SW1"; zephyr,code = <INPUT_KEY_0>; };
        sw2: switch_2 { gpios = <...>; label = "SW2"; zephyr,code = <INPUT_KEY_1>; };
        sw3: switch_3 { gpios = <...>; label = "SW3"; zephyr,code = <INPUT_KEY_2>; };
        sw4: switch_4 { gpios = <...>; label = "SW4"; zephyr,code = <INPUT_KEY_3>; };
    };

    aliases {
        sw0 = &sw1;
        sw1 = &sw2;
        sw2 = &sw3;
        sw3 = &sw4;
    };
};
```

Specific values to fill in during Phase 1 implementation:

- I²C bus instance number, pin assignment, and the actual 7-bit addresses for each sensor (visible from `i2c scan` if not from the schematic).
- PDM instance number (`pdm20` vs `pdm21`) and exact pin assignment for MIC_CLK / MIC_DATA.
- UART instance + RX/TX pins for the shell console.
- Button pin numbers and polarity (pull-up vs pull-down decided from the schematic).

---

## 5. First application — `app/led-test/`

### 5.1 File layout

```
app/led-test/
├── CMakeLists.txt
├── prj.conf
├── sample.yaml
├── boards/
│   └── nrf54l15dk_nrf54l15_cpuapp.overlay      # 1-line: #include "../../../../boards/phial-common.dtsi"
└── src/
    ├── main.c
    ├── led_anim.c / led_anim.h
    └── shell_cmds.c
```

### 5.2 `main.c`

Thin: wait for the `gpio-leds` device to be ready, spawn the animation thread at low priority, set initial pattern, return. The shell thread takes over the foreground.

### 5.3 `led_anim.c` — animation engine

Cooperative state machine in its own thread, driven by:

```c
enum led_pattern {
    LED_PATTERN_OFF,           /* all off */
    LED_PATTERN_ALL_ON,        /* sanity */
    LED_PATTERN_CHASE,         /* single LED rotates 1→16 */
    LED_PATTERN_RING_SWEEP,    /* ring (1–12) sweeps, center idle */
    LED_PATTERN_CENTER_PULSE,  /* center (13–16) toggles, ring idle */
    LED_PATTERN_CLOCK,         /* ring fills like seconds hand */
    LED_PATTERN_BREATHING,     /* Phase 2 only — requires pwm-leds */
};

struct led_anim_state {
    enum led_pattern pattern;
    uint32_t         period_ms;
    /* internal step counter etc. */
};
```

LEDs are driven via the **Zephyr LED API** (`led_on(dev, idx)` / `led_off(dev, idx)`), bound to the `leds` (gpio-leds) node. Indexed 0–15 matching the aliases.

### 5.4 `shell_cmds.c` — `phial` subcommand tree

```
phial pattern <off|all|chase|ring|center|clock|breathe>
phial period <ms>
phial led <0-15> <on|off>
phial status
```

Built-in `led on/off/set_brightness leds N` from `CONFIG_LED_SHELL=y` remains available for raw poking. `phial pattern …` is the demo-control surface.

### 5.5 Success criteria — Phase 1 minimal build

1. Image boots; shell banner appears on the UART console at the configured baud.
2. `phial pattern chase` rotates one LED through all 16 positions, no skips or stuck LEDs.
3. `phial led <n> on` / `... off` for every `n` ∈ 0..15 toggles exactly one LED.
4. `kernel uptime` and `device list` work (shell is healthy and bindings are visible).
5. `gpio` and `i2c` shell commands are present and responsive (bus probing works even though no I²C code runs in this app).
6. `kernel reboot warm` reboots cleanly.

---

## 6. Shared shell scaffolding — `conf/shell-common.conf`

```
# Shell core
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="phial:~$ "
CONFIG_SHELL_HISTORY=y
CONFIG_SHELL_TAB=y
CONFIG_SHELL_TAB_AUTOCOMPLETION=y
CONFIG_SHELL_LOG_BACKEND=y

# Built-in command modules
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

# Logging
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_BACKEND_UART=y

# Console
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_PRINTK=y
```

Pulled in by each app's `CMakeLists.txt`:

```cmake
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../../conf/shell-common.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(led_test)
```

### 6.1 Bringup workflow unlocked at the prompt

```
phial:~$ device list                       # confirm bindings probed
phial:~$ i2c scan i2c1                     # discover BME280 / LIS2DH / ATECC608B / nPM2100 addrs
phial:~$ i2c read i2c1 0x76 0xD0           # BME280 chip ID register
phial:~$ gpio conf gpio2 7 out             # raw-poke LED16
phial:~$ gpio set gpio2 7 1
phial:~$ led on leds 5                     # via LED API
phial:~$ sensor get bme280                 # Phase 2, once binding wired
phial:~$ regulator vset npm2100_ldo1 1800000
phial:~$ phial pattern chase
phial:~$ kernel reboot warm
```

A complete bringup toolkit before any sensor code is written — every peripheral on the board is pokeable from the prompt.

---

## 7. Phase ladder

| Phase | Build target | Adds | Exit criterion |
|---|---|---|---|
| **0. Bootstrap** | n/a | Repo skeleton, `west.yml`, `boards/phial-common.dtsi`, `conf/shell-common.conf`, README | `west init -l . && west update` succeeds; empty workspace builds |
| **1. Minimal LED bringup** | `nrf54l15dk/nrf54l15/cpuapp` (secure-only, **no MCUboot**, **no sysbuild**, **no TF-M**) | `app/led-test/` with gpio-leds driver, `phial` shell, `LED_PATTERN_CHASE` | All 16 LEDs cycle visibly in chase; `i2c scan` returns ≥4 devices; `kernel reboot warm` works |
| **2. PWM + RDT-canonical scaffold** | `nrf54l15dk/nrf54l15/cpuapp/ns` (TF-M, **sysbuild**, **MCUboot**, **`pm_static.yml`**) | `pwm-leds` for fade-capable LEDs, `LED_PATTERN_BREATHING`, signed/bootloaded image | DFU-ready signed image boots through MCUboot; breathing pattern smooth; same shell still works |
| **3. BLE bringup** | same as Phase 2 | `app/ble-test/` (new app), `CONFIG_BT_SHELL=y`, peripheral advertising as `phial-XXXXXX` | nRF Connect mobile app sees `phial-*` advert, connects, MTU exchange completes; shell-driven advertise/connect cycles |
| **4. Golioth Pouch port** | same as Phase 2 | `app/pouch/`, Pouch transport, golioth-firmware-sdk integration | Pouch device appears in Golioth console; reaches steady-state with credentials provisioned |

### 7.1 Rationale for the phase ordering

Phase 1 deliberately removes every variable that isn't the board file itself. If the LEDs don't light, the failure space is exactly devicetree pins, polarity, or MCU clock setup — no MCUboot, no TF-M, no partitioning to debug in parallel. Phase 2 layers on the full RDT stack only after we have evidence the board file is right. Every later failure is then a stack failure, not a foundational failure.

Each phase produces a tagged commit (`phase-0-bootstrap`, `phase-1-minimal-led`, `phase-2-rdt-canonical`, …) so we can `git checkout phase-1` and demonstrate the minimal build still works. Phases 1 and 2 should be bisectable — Phase 2 is a single coherent commit on top of Phase 1.

### 7.2 Explicitly cut from initial scope (YAGNI)

- PWM channel mapping for LEDs — deferred to start of Phase 2.
- BLE audio / PDM mic capture — orthogonal; not on path to Pouch.
- ATECC608B integration — Pouch doesn't currently require it; if it does later, that's a separate sub-project.
- nPM2100 fuel-gauge / battery monitor logic — orthogonal; defer.
- CI pipeline — set up after Phase 1 works locally; not a Phase 0 blocker.
- A proper custom Zephyr board file (under `boards/<vendor>/phial/`) — overlay-on-DK is good enough until proven otherwise.

---

## 8. Open items to resolve during implementation

These are deferred-but-tracked. Each gets resolved in the implementation plan or during Phase 1 itself:

1. **LED polarity** — assumed active-high; verify against schematic.
2. **I²C bus instance number** + pin assignment + per-sensor 7-bit addresses.
3. **PDM instance** (`pdm20` vs `pdm21`) and MIC_CLK / MIC_DATA pin numbers; cross-check against nRF54L15 pin-routing table.
4. **UART (shell console) instance** and RX/TX pins.
5. **Button pin numbers and polarity** (pull-up vs pull-down).
6. **LF21115TMR (U104)** — confirm what part this is and whether the firmware needs to be aware of it.
7. **Y101 / Y102 crystals** — confirm frequencies; HF32M and LFXO LF32K is the expectation but the dtsi needs to declare them correctly.
8. **U102 antenna / matching network** — typically no firmware concern, but confirm no GPIO control is involved.

---

## 9. Out of scope for this design

These are valid future work but **not** in this spec; each would get its own design doc when its time comes:

- BLE audio over Pouch / LE Audio.
- Custom MCUmgr commands for Pouch.
- Production provisioning (secure element keys, Golioth credentials).
- Power-budget characterisation and sleep-current optimisation.
- Manufacturing-test fixture protocol.
