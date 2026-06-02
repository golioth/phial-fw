# Design: `app/sensor-test` — LIS2DH12 tilt → LED control plane

- **Date:** 2026-06-02
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild)
- **Depends on:** I²C bring-up (`i2c30`, SDA P0.00 / SCL P0.01) and the 16-LED
  `gpio-leds` map, both already in `boards/phial-common.dtsi`.

## Goal

A second app (sibling to `app/led-test/`) that doubles as a **sensor bring-up
harness** and a **live tilt demo**: the on-board LIS2DH12 accelerometer becomes
a control plane for the 16 LEDs. Tilt the board and a single ring LED lights on
the **downhill** edge (the "marble rolls to the low point" metaphor). Hold it
flat and the 4 center LEDs light to mean "level / centered."

Z runs vertically through the board, so the in-plane (X/Y) component of gravity
encodes tilt direction; its magnitude encodes how far from level.

## Behavior

| Board state | LEDs |
|-------------|------|
| Flat / level (in-plane gravity below threshold) | 4 center LEDs (labels LED13–16 = **indices 12–15**) on, ring off |
| Tilted past threshold | Exactly one ring LED, on the **downhill** side; center off |

### LED indexing convention (read first — prevents off-by-one)

Physical silkscreen **labels are 1-based** (LED01…LED16). The Zephyr `led` /
`gpio-leds` API is **0-based**: driver index = label − 1. So LED01 = index 0,
LED12 = index 11, LED13 = index 12, … LED16 = index 15 — matching the
`led_on(leds, i)` calls already in `app/led-test/main.c`.

**All code (the `sector_to_led` table, the center-cluster writes) uses 0-based
indices.** This document mentions 1-based labels only in prose for human
readability; every array/loop value is 0-based. Example: "center cluster
LED13–16" → `led_on(leds, 12..15)`.

- Ring resolution: 12 sectors of 30° (one LED each). Single LED only — no
  blending (LEDs are plain GPIO on/off; brightness/PWM is a later phase).
- Transitions use hysteresis so the display does not flicker between
  level↔rolled or across sector seams.

## Architecture

Three units with clear boundaries:

1. **`tilt.c` / `tilt.h` — pure logic, no hardware.** Converts an (ax, ay)
   reading into a display target. Unit-tested on the host.
2. **`main.c` — control loop + glue.** Owns the sensor and LED devices, samples
   at ~50 Hz, low-pass filters, calls `tilt`, renders to the LEDs, and registers
   the `tilt` shell command.
3. **Devicetree** — the LIS2DH12 node, added once to the shared
   `boards/phial-common.dtsi`.

### Pure logic — `tilt.c` / `tilt.h`

```c
/* Stateless: returns -1 when within the level deadzone, else compass bucket
 * 0..11 (0 = North, increasing clockwise), after calibration is applied. */
int tilt_sector(float ax, float ay);

/* Stateful wrapper adding hysteresis on both the level<->rolled boundary and
 * the sector seams. Returns TILT_LEVEL (-1) or a sector 0..11. */
int tilt_update(struct tilt_state *st, float ax, float ay);
```

Calibration / tuning constants live in `tilt.h` so they are adjustable without
touching logic:

| Constant | Meaning |
|----------|---------|
| `TILT_OFFSET_DEG` | Rotation between the chip's +X axis and ring North |
| `TILT_SWAP_XY` | Swap X/Y if the chip is mounted rotated 90° |
| `TILT_INVERT_X`, `TILT_INVERT_Y` | Per-axis sign flips for mirror/rotation |
| `TILT_ENTER_MS2` | In-plane |g| to leave level and show a ring LED |
| `TILT_EXIT_MS2` | In-plane |g| to fall back to level (`< ENTER` = hysteresis) |
| `TILT_SEAM_MARGIN_DEG` | Extra angle past a seam before the lit sector changes |

Math: `angle = atan2f(ay, ax)` (single-precision), offset applied, quantized to
the nearest 30° bucket. The default constants are an educated guess; the real
chip-to-ring rotation is dialed in on the bench (see Calibration).

### Control loop — `main.c`

- Acquire devices: `leds` (`gpio_leds`) and `accel` (`DEVICE_DT_GET` of the
  LIS2DH12 node). Fail loudly via `LOG_ERR` + return if either is not ready.
- Loop at ~50 Hz (`k_msleep(20)`):
  1. `sensor_sample_fetch()` + `sensor_channel_get()` for ACCEL_X/Y/Z → float m/s².
  2. EMA low-pass on X/Y (α ≈ 0.3) to suppress jitter.
  3. `tilt_update()` → target.
  4. **Render only on change** (avoid redundant GPIO writes / flicker):
     - `TILT_LEVEL` → ring off, center LEDs 13–16 on.
     - sector `n` → all off, light `sector_to_led[n]`.
- `sector_to_led[12]` maps compass bucket (0 = North, clockwise) → **0-based
  driver index**. From the led-test layout: sector 0 (N) → LED12 = index **11**;
  sector 3 (E) → LED03 = index **2**; sector 6 (S) → LED06 = index **5**;
  sector 9 (W) → LED09 = index **8**; the eight in-between buckets fill the
  remaining ring positions. Keeping it an explicit 12-entry table makes the
  physical mapping reviewable and adjustable independent of the angle math.
  (This is a *separate* table from led-test's `inner_for_outer`, which maps the
  other direction — outer ring → center cluster — and is not reused here.)

### Tuning shell command — `tilt`

- `tilt status` — print filtered ax/ay/az, in-plane magnitude, raw + applied
  angle, sector, and target LED. The primary calibration aid.
- `tilt offset <deg>` — set a **volatile** offset live (overrides the build-time
  default until reboot) so the chip-vs-ring rotation can be tuned without
  reflashing. The final value is then baked into `tilt.h`.

Registered app-side via `SHELL_STATIC_SUBCMD_SET_CREATE` + `SHELL_CMD_REGISTER`
in `main.c` (distinct from the built-in `sensor` command that
`CONFIG_SENSOR_SHELL` provides).

## Devicetree change

Add the sensor node to the **shared** overlay (one description of every
peripheral, per repo convention). It is inert for apps that do not enable
`CONFIG_LIS2DH`, so `app/led-test` is unaffected.

```dts
&i2c30 {
    accel: lis2dh12@18 {
        compatible = "st,lis2dh12";
        reg = <0x18>;
        status = "okay";
    };
};
```

No `irq-gpios`: the app polls, so the INT line need not be routed (and we have
not confirmed it is).

## Build / config

`app/sensor-test/` mirrors `app/led-test/`:

- **CMakeLists.txt** — same pattern: `EXTRA_CONF_FILE += ../../conf/shell-common.conf`,
  then `target_sources(app PRIVATE src/main.c src/tilt.c)`.
- **boards/nrf54l15dk_nrf54l15_cpuapp.overlay** — `#include "../../../boards/phial-common.dtsi"`.
- **prj.conf** (on top of `shell-common.conf`, which already sets
  `CONFIG_SENSOR_SHELL=y`):

```
CONFIG_LED=y
CONFIG_LED_GPIO=y
CONFIG_GPIO=y
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_LIS2DH=y
CONFIG_CBPRINTF_FP_SUPPORT=y    # %f in shell output
CONFIG_FPU=y                    # M33 hardware float for atan2f
CONFIG_MAIN_STACK_SIZE=2048
```

For the RTT-fallback console block, **copy `app/led-test/prj.conf`'s console
section verbatim, including its comments** — it carries a hard-won warning not
to also enable `CONFIG_LOG_BACKEND_RTT` (it would fight the shell over RTT
channel 0). The relevant symbols are `CONFIG_USE_SEGGER_RTT=y`,
`CONFIG_RTT_CONSOLE=y`, `CONFIG_SHELL_BACKEND_RTT=y`, `CONFIG_UART_CONSOLE=y`
(RTT owns the console; UART shell rides its own backend from
`shell-common.conf`). Do not re-derive this block from scratch.

- **sample.yaml** — `build_only`, `platform_allow: nrf54l15dk/nrf54l15/cpuapp`,
  tags `phial`, `sensor`, `accel`.

## Testing

**Unit (TDD, `tests/tilt/`, native_sim ztest)** — the pure logic, no hardware:
- Deadzone: |g_xy| below `EXIT` → `TILT_LEVEL`; above `ENTER` → a sector.
- Each cardinal + diagonal vector → expected sector (with default calibration).
- Seam no-flicker: an angle dithering ±a hair around a seam holds one sector.
- Calibration: `TILT_OFFSET_DEG`, `TILT_SWAP_XY`, invert flags produce the
  expected rotated/mirrored sector.

**Hardware bring-up:**
1. `sensor get <name>` returns sane X/Y/Z (≈ +9.8 on whichever axis is up).
   The `<name>` is the devicetree node name as shown by `device list` — i.e.
   `lis2dh12@18`, not the `accel` label. Confirm the exact string on hardware.
2. Tilt N/E/S/W → expect LED12 / LED03 / LED06 / LED09 (after calibration).
3. Flat → center cluster (13–16) on.
4. Use `tilt status` / `tilt offset` to find the offset; bake into `tilt.h`.

## Calibration plan

The LIS2DH12's X/Y axes will not necessarily align with ring North, and the
mounting rotation/mirroring is not known from the repo (schematic + placement
not in-tree). Default constants are a starting guess. On the bench: tilt toward
a known LED, read `tilt status`, adjust `tilt offset` (and, if needed, the
swap/invert flags) until the lit LED matches the tilt, then hardcode the result
in `tilt.h`.

## Out of scope (later phases)

- PWM brightness to encode tilt magnitude (needs `pwm-leds`, Phase 2).
- Interrupt/trigger-driven sampling (needs the INT line routed).
- BME280 / ATECC608B / nPM2100 — other devices on the bus; separate work.
- Persisting calibration across reboot (Settings); volatile tuning only for now.

## Open questions

- Exact chip-to-ring rotation → resolved empirically via the calibration plan.
- Whether any ring LED sits exactly at a cardinal vs. between two (affects the
  `sector_to_led` table) → confirmed against the physical board during bring-up.
