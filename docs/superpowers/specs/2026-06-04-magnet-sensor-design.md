# Design: `app/sensor-test` — LF21115TMR magnet switch (LED-flash input)

- **Date:** 2026-06-04
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild)
- **Depends on:** the 16-LED `gpio-leds` map in `boards/phial-common.dtsi` and the
  existing tilt→LED control loop
  ([2026-06-02-sensor-test-tilt-led-design.md](2026-06-02-sensor-test-tilt-led-design.md)).
  Sibling to the BME280 env feature
  ([2026-06-03-bme280-env-sampling-design.md](2026-06-03-bme280-env-sampling-design.md)).

## Goal

Add a third sensor to the `sensor-test` demo: a **Littelfuse LF21115TMR** omni-polar
TMR magnetic switch on **P1.13**. When a magnet is present, the whole 16-LED board
**flashes on/off at 500 ms** (1 Hz), overriding the tilt display, and each on/off
toggle is logged to the console. When no magnet is present the board is silent (no
logs) and the normal tilt→LED display runs. A `mag_present()` accessor is the seam a
future BLE/Golioth transport can read; that downstream use is out of scope here.

## The part

LF21115TMR — omni-polar, **push-pull digital output**, ~17 Gauss trip, built-in
Schmitt hysteresis (clean edges, no debounce), ~200 nA, switches up to 50 Hz, 1.8–5.5 V.
Output is **HIGH at power-on with no field, and pulls LOW when a magnet of either pole
is near**. Push-pull drives both rails, so **no pull resistor** is needed — configure
the nRF pin as a plain input.

P1.13 is free: it was the DK's `button0`, and `phial-common.dtsi` already
`/delete-node/`s the DK buttons. It is not in the Phial LED map.

## Non-goals

- **No analog / sensor-API path.** This is a digital GPIO line, not an I²C sensor;
  no `CONFIG_SENSOR` involvement.
- **No interrupt/low-power path.** Polling in the existing loop is sufficient for a
  hand-waved magnet (see Architecture). Power optimization is out of scope.
- **No BLE/Golioth transport.** `mag_present()` is the seam; the transport is later work.

## Behavior

| Magnet | LEDs | Console |
|--------|------|---------|
| Present (pin low) | All 16 flash: 500 ms on, 500 ms off (1 Hz) | `LOG_INF` on each toggle: `magnet: LEDs ON` / `magnet: LEDs OFF` |
| Absent (pin high) | Normal tilt→LED display resumes | Silent (no magnet logging) |

The accel sampling + EMA filtering continue every tick regardless of magnet state, so
the tilt target is current the instant the magnet leaves and the display resumes
correctly.

## Architecture

The magnet feature and the tilt renderer both want to drive the LED device. The design
keeps **all LED writes on the single main-loop thread** so there is no concurrency on
the LED device. Three pieces:

1. **`mag.c` / `mag.h` — thin GPIO module.** Owns the magnet GPIO and exposes
   `mag_init()` / `mag_present()`. No thread, no lock — `mag_present()` is a pin read.
   This is the reusable seam (a future BLE module reads `mag_present()`).
2. **`main.c` — display arbiter.** The existing 50 Hz loop polls `mag_present()` each
   tick and decides what the LEDs show: blink-all when a magnet is present, tilt render
   otherwise. This is the one place LEDs are written.
3. **Devicetree** — the magnet GPIO, added once to the shared `phial-common.dtsi`.

### Why poll in the main loop (not interrupt/thread)

The loop already ticks every 20 ms and is already the sole LED writer. Folding magnet
detection + the 500 ms blink into it adds no concurrency and no ISR/work plumbing, and
50 Hz polling easily resolves a human-scale magnet event (the part itself only switches
to 50 Hz). A GPIO interrupt + `k_timer`, or a dedicated thread, would each introduce a
second LED-writing context that must lock against the tilt rendering — complexity this
demo does not need.

### Public interface — `mag.h`

```c
/* Configure the LF21115TMR GPIO line (P1.13) as an input.
 * Returns 0 on success, -ENODEV if the GPIO is not ready. */
int  mag_init(void);

/* True when a magnet is present (the sensor pulls the line low; the DT spec
 * is GPIO_ACTIVE_LOW, so the logical pin value is 1 in that case). */
bool mag_present(void);
```

### Devicetree change

Add to the shared `boards/phial-common.dtsi`:

```dts
/ {
    zephyr,user {
        /* LF21115TMR omni-polar TMR magnet switch on P1.13. Push-pull output,
         * low = magnet present, so ACTIVE_LOW makes the logical value 1 = present.
         * No pull needed (push-pull drives both rails). */
        mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
    };
};
```

`mag.c` resolves it with `GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), mag_gpios)` and
configures it `GPIO_INPUT`. Placed in the shared dtsi (consistent with the accel/BME280
"board hardware" precedent); harmless to apps that never reference the property.

### main.c loop changes

Loop-local state (declared in `main()` before the loop): `bool in_magnet_mode = false;`,
`bool blink_on;`, `int64_t last_blink_ms;` (the latter two are (re)initialized on the
absent→present edge, so their initial values don't matter). Pseudostructure of the
per-tick logic (replaces the current "render only on tilt change" block):

```
sample accel + EMA (unchanged)
t = tilt_update(...)                       // keep tilt state current every tick

if (mag_present()) {
    if (!in_magnet_mode) {                 // absent -> present edge
        in_magnet_mode = true;
        blink_on = false;                  // so the first toggle below turns LEDs ON
        last_blink_ms = 0;                 // force an immediate first toggle, no stale wait
    }
    if (k_uptime_get() - last_blink_ms >= 500) {
        last_blink_ms = k_uptime_get();
        blink_on = !blink_on;
        for all 16 LEDs: blink_on ? led_on : led_off;
        LOG_INF("magnet: LEDs %s", blink_on ? "ON" : "OFF");
    }
} else {
    if (in_magnet_mode) {                  // present -> absent edge
        in_magnet_mode = false;
        force render(leds, g_target);      // restore tilt display
    }
    // normal path: render on tilt change, as today
    if (t != g_target) { g_target = t; render(leds, t); }
}
```

### Kconfig and build

- No new Kconfig: `CONFIG_GPIO=y` is already set; this is a plain GPIO input.
- `CMakeLists.txt`: add `src/mag.c` to `target_sources(app PRIVATE ...)`.

## Error handling

| Condition | Handling |
|-----------|----------|
| Magnet GPIO not ready at init | `mag_init()` returns `-ENODEV`; `main()` logs `LOG_WRN` and continues. Magnet feature disabled; tilt + env unaffected. |
| Magnet never detected | No blink, no logging — the silent/normal path. |

## Testing

- **No new host unit test.** Like `env.c`, this is hardware glue (a GPIO read) plus
  loop policy; there is no pure logic to isolate (unlike `tilt.c`'s angle math).
- **Build verification:** clean pristine build for `nrf54l15dk/nrf54l15/cpuapp` with
  `--no-sysbuild`.
- **On-hardware verification:**
  1. Bring a magnet near → all 16 LEDs flash on/off at ~1 Hz, and the console prints
     `magnet: LEDs ON` / `magnet: LEDs OFF` on each toggle.
  2. Remove the magnet → flashing stops, logging stops, and the tilt→LED display
     resumes correctly (tilt a bit to confirm the ring/center LEDs respond again).
  3. The BME280 `env:` log line still appears every 10 s throughout (env unaffected).

## Files touched

| File | Change |
|------|--------|
| `boards/phial-common.dtsi` | Add `zephyr,user { mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>; }` |
| `app/sensor-test/src/mag.h` | New — `mag_init`, `mag_present` |
| `app/sensor-test/src/mag.c` | New — GPIO config + read |
| `app/sensor-test/CMakeLists.txt` | Add `src/mag.c` to sources |
| `app/sensor-test/src/main.c` | Poll magnet each tick; blink-all + log when present; restore tilt on release |
