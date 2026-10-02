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
