# Phial Firmware

## Project status (October 2026)

**Idea:** Multi-application Zephyr/NCS firmware repo for the Phial board
(KiCad project `pilulith`, Nordic nRF54L15-QFAA), following Golioth
Reference Design Template conventions adapted for several test/demo apps
living side-by-side. The design spec phases the work: LED bringup → shell
diagnostics → BLE → Golioth cloud via Pouch.

**State:** 16 commits, 2026-05-14 to 2026-06-02. Phase 0/1 bringup is
complete on hardware per commit history: `app/led-test` builds and boots
on `nrf54l15dk/nrf54l15/cpuapp`, drives all 16 LEDs (walk + chase
animations), and exposes an interactive shell over Segger RTT and UART20
(routed to the Phial debug header, P1.08 TX / P1.07 RX). Version 0.1.0,
unreleased; `CHANGELOG.md` tracks everything under `[Unreleased]`.

**SDK / toolchain (as committed):** `west.yml` pins
**golioth-firmware-sdk v0.21.0** (latest upstream is v0.22.0, Dec 2025),
whose `west-ncs.yml` pulls **NCS / sdk-nrf v3.1.1** with
**sdk-zephyr `ncs-v3.1.1`** (Zephyr 4.1-based, mid-2025). That is roughly
three NCS/Zephyr feature releases behind the Zephyr 4.4-era baseline
current in October 2026 — expect upgrade work (board naming already uses
the HWMv2 `nrf54l15dk/nrf54l15/cpuapp` form, which helps).

**Known gaps:**

- **Uncommitted work in the working tree (as of 2026-10-02, rescued
  elsewhere but never committed here):**
  - `west.yml` (modified): adds the `pouch` project
    (`2d66b836471c03ed9bbee11e5fb033faf94e7a29`) for BLE-to-cloud
    connectivity.
  - `app/fleet-demo/` (untracked): full Golioth fleet-management demo —
    Pouch BLE GATT transport, Settings/Stream/LightDB State/OTA services,
    MCUboot/sysbuild config, LED pattern control from the cloud. Has its
    own README. A `app/fleet-demo/build/` tree of build artifacts is also
    present and must not be committed/published.
  - `app/led-test/src/led_anim.h` (untracked): animation header for
    led-test.
  - `scripts/fleet-setup.py` (untracked): Golioth cloud provisioning
    helper (projects, devices, PSK credentials, OTA packages/deployments;
    takes `GOLIOTH_API_KEY` from the environment — no key material in the
    file, though its `--psk secret` / `--psk-id phial-001-psk` defaults
    are placeholders that must not ship).
- **Broken link:** the "T2 freestanding workspace" link below points to
  `https://docs.zephyrproject.org/latest/develop/west/workspaces.html` (mangled); the correct
  URL is `https://docs.zephyrproject.org/latest/develop/west/workspaces.html`.
- The Applications table below lists only `app/led-test`; the uncommitted
  `app/fleet-demo` is undocumented in this README.
- Phases 2–4 from the design spec (PWM LED dimming, BLE, committed Pouch
  app) are not present in the committed tree.
- No CI; nothing published (no releases/tags).
- Secrets sweep (2026-10, tracked files): clean — no credentials, keys,
  device IDs, personal emails, or Wi-Fi credentials in any tracked file.

---

Firmware for the Phial board (KiCad project `pilulith`, Nordic nRF54L15-QFAA).
Multi-app repo following the [Golioth Reference Design
Template](https://github.com/golioth/reference-design-template) conventions,
adapted for multiple test/demo applications living side-by-side.

## Applications

| Path               | Purpose                                                                 |
|--------------------|-------------------------------------------------------------------------|
| `app/led-test/`    | Drive all 16 LEDs; exercise Zephyr shell + I²C + GPIO                    |
| `app/sensor-test/` | Tilt→LED compass (LIS2DH), BME280 env logging, LF21115TMR magnet switch |
| `app/buzzer-test/` | Half-octave test tones (800→8000 Hz) on the P0.04 buzzer via the shell  |
| `app/mic-test/`    | Record the MP34DT05 PDM mic (button-held) to a flash WAV — see its README |
| `app/sensor-pouch/` | BME280 → Golioth Stream over **pouch** (BLE GATT); cloud `LED` setting (1-16) lights an LED. Built with `--sysbuild` (MCUboot). |

## Setup

This repo is a [T2 freestanding
workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html).
After cloning, initialize and update west to pull NCS as a sibling tree:

    cd phial-fw
    west init -l .
    west update                 # first time: ~2 GB download, ~5–10 min

## Build & flash — `app/led-test`

Phase 1 build target (secure-only, no bootloader):

    west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/led-test
    west flash

Open a serial console at **115200 8N1** (see wiring below). You should see:

    *** Booting Zephyr OS build ... ***
    phial:~$

Try:

    phial:~$ device list
    phial:~$ i2c scan i2c@104000
    phial:~$ kernel reboot warm

## Serial console wiring (J-Link VCOM)

The Phial board has no on-board USB-serial. The shell/console UART (`uart20`,
115200 8N1, no flow control) leaves the nRF54L15 on:

| Signal | nRF pin   | KiCad net   | Tag-Connect J103 |
|--------|-----------|-------------|------------------|
| TX     | **P1.08** | `/nRF52_TX` | pin 7            |
| RX     | **P1.07** | `/nRF52_RX` | pin 8            |

It is read through the SEGGER **J-Link Virtual COM Port**. The
Tag-Connect → 20-pin-J-Link breakout MUST map these so the device's TX reaches
the J-Link's *receive* pin — they are **not** wired "TX→TX":

| nRF signal | → J-Link 20-pin pin        |
|------------|----------------------------|
| TX (P1.08) | **pin 17** (J-Link-Rx, in) |
| RX (P1.07) | **pin 5**  (J-Link-Tx, out)|
| GND        | any even pin (4,6,…,20)     |

> ⚠️ **Pin-swap gotcha:** a breakout that wires TX→pin 5 / RX→pin 17 (the
> intuitive-but-wrong "TX-to-TX") yields a console that enumerates on the host
> but stays completely silent. If you see nothing, check this mapping first.
> Note J-Link VCOM only streams while a SWD session is active. The RTT shell
> (`CONFIG_SHELL_BACKEND_RTT`) rides SWD independently and is the fallback
> console when UART is silent — open it with `JLinkRTTViewer` or
> `nrfutil device rtt`.

## I²C bus (`i2c30`)

The sensor bus is SERIAL30 as TWIM: **SDA = P0.00, SCL = P0.01**, 100 kHz.
`i2c scan i2c@104000` reports:

| Addr | Device                                   |
|------|------------------------------------------|
| 0x18 | LIS2DH 3-axis accelerometer (U10)        |
| 0x60 | ATECC608B secure element                 |
| 0x76 | BME280 temperature / humidity / pressure |

The nPM2100 PMIC is DNP on the current board, so its expected ~0x74 address is
absent (3 devices found, not 4).

## Design docs

See `docs/superpowers/specs/` for design history and `docs/superpowers/plans/`
for implementation plans.
