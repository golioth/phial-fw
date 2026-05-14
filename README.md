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
