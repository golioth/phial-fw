# Phial Firmware

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
