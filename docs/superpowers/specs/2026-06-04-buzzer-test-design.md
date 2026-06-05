# Design: `app/buzzer-test` — P0.04 buzzer test tones

- **Date:** 2026-06-04
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild)
- **Depends on:** the shared `boards/phial-common.dtsi` (for the `zephyr,user` node and
  the shell-common config). New app, sibling to `app/led-test` and `app/sensor-test`.

## Goal

A new shell-driven app that plays test tones on the Phial's buzzer (a ~4 kHz
transducer on **P0.04**): a sweep stepping up in **half-octave** steps from
**800 Hz to 8000 Hz**, plus a single-tone command. Purpose is hardware bring-up /
audible verification of the buzzer.

## Key hardware constraint (why no PWM)

On the nRF54L15, GPIO is split across power domains: **P0 is the low-power domain**
(pins P0.00–P0.04 on this package), while **PWM20/21/22 live in the peripheral
domain and can only route to P1** pins. There is no low-power PWM instance, so a
**PWM peripheral cannot drive P0.04**. (This differs from nRF52/53/91, which have a
single GPIO domain where PWM reaches any pin — a common porting surprise.)

The domain rule restricts *peripheral-routed* signals, not CPU writes to a pin, so
we generate the tone by **toggling P0.04 in software**. P0.04 is free (it was the
DK's `button_0`, and `phial-common.dtsi` already `/delete-node/`s the DK buttons).

## Non-goals

- **No PWM, no I²S, no audio subsystem.** Plain GPIO square wave only.
- **No LEDs or sensors.** This app is buzzer-only.
- **No tone quality beyond a square wave.** A ~4 kHz resonant buzzer is loudest near
  4 kHz and weak at the band edges (800 Hz especially); that uneven loudness is
  expected and not something we compensate for.

## Tone generation

For each tone, toggle P0.04 with a 50 % duty square wave for the tone's duration,
using **busy-wait timing** (`k_busy_wait(half_period_us)`):

```
half_period_us = 1000000 / (2 * hz)
toggles        = duration_ms * 1000 / half_period_us   (even count -> ends LOW)
for each toggle: gpio_pin_toggle_dt(&buzzer); k_busy_wait(half_period_us)
ensure the pin is left LOW at the end
```

- **Accuracy:** `k_busy_wait` is microsecond-calibrated, so pitch is accurate to ~1 %
  even at 8 kHz (62 µs half-period). Duty cycle is a clean 50 %.
- **Trade-off (accepted):** this **blocks the calling (shell) thread** for the tone /
  sweep duration (~2.5 s for the full sweep) and spins the CPU. Fine for a bench
  test buzzer. Interrupts still run during `k_busy_wait`, so background work (e.g.
  logging) is not starved; only the shell is briefly unresponsive.
- **Rejected alternative:** a hardware TIMER via the Zephyr counter API toggling the
  pin in an alarm ISR (non-blocking, rock-solid) — more setup (DT timer node,
  `CONFIG_COUNTER`, alarm callbacks) than this test utility warrants. Documented here
  in case blocking ever becomes a problem.
- **Rejected alternative:** `k_timer` periodic toggle — system-tick resolution
  (~30 µs at 32 kHz) makes 8 kHz tones coarse and impure.

## Tones

Half-octave series (×√2), rounded to whole Hz, with 8000 as an explicit ceiling:

```
800, 1131, 1600, 2263, 3200, 4525, 6400, 8000   (8 tones)
```

The last step (6400→8000) is slightly under a half-octave; that is intentional to
hit the requested 8000 Hz ceiling.

## Shell interface

The `buzzer` command (registered in `buzzer.c`, like `env`/`tilt` in sensor-test):

| Command | Behavior |
|---------|----------|
| `buzzer sweep` | Play all 8 tones in order, ~250 ms each with a ~100 ms gap. Logs each tone's frequency. |
| `buzzer tone <hz> [ms]` | Play a single tone at `<hz>` for `[ms]` (default 250). Guards against 0 / out-of-range frequency. |

## Architecture

Three units, mirroring the sensor-test module style:

1. **`buzzer.c` / `buzzer.h` — the buzzer module.** Owns the P0.04 GPIO and exposes
   `buzzer_init()`, `buzzer_tone(uint32_t hz, uint32_t ms)`, `buzzer_sweep(void)`, and
   registers the `buzzer` shell command. Single responsibility: make the pin beep.
2. **`main.c` — startup glue.** Calls `buzzer_init()` and idles; the shell drives all
   playback.
3. **Devicetree** — the buzzer GPIO, added to the shared `phial-common.dtsi`.

### Public interface — `buzzer.h`

```c
/* Configure the buzzer GPIO (P0.04) as an output, idle LOW.
 * Returns 0 on success, -ENODEV if the GPIO is not ready. */
int  buzzer_init(void);

/* Play a square-wave tone of `hz` for `ms` milliseconds (blocking). No-op for
 * hz == 0. The pin is left LOW afterward. */
void buzzer_tone(uint32_t hz, uint32_t ms);

/* Play the fixed half-octave sweep (800..8000 Hz), blocking. */
void buzzer_sweep(void);
```

### Devicetree change

Add a property to the **existing** `zephyr,user` node in `boards/phial-common.dtsi`
(the magnet feature created that node; add to it, do not create a second one):

```dts
/ {
    zephyr,user {
        mag-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;     /* existing */
        buzzer-gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;  /* new: P0.04 buzzer */
    };
};
```

`buzzer.c` resolves it with `GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), buzzer_gpios)`
and configures it `GPIO_OUTPUT_INACTIVE` (idle LOW). Placed in the shared dtsi
(board hardware, consistent with accel/BME280/magnet); harmless to apps that never
reference the property — including the existing apps, which do not.

### Build files (new app skeleton)

- `app/buzzer-test/CMakeLists.txt` — pulls in `../../conf/shell-common.conf` via
  `EXTRA_CONF_FILE` (like sensor-test), `project(buzzer_test)`, sources `src/main.c`
  and `src/buzzer.c`.
- `app/buzzer-test/prj.conf` — `CONFIG_GPIO=y`; FP printing not needed (integer Hz);
  RTT/UART console lines mirroring the other apps so the shell is reachable.
- `app/buzzer-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` — `#include`s
  `../../../boards/phial-common.dtsi`.
- `app/buzzer-test/sample.yaml` — mirror the sibling apps.

## Error handling

| Condition | Handling |
|-----------|----------|
| Buzzer GPIO not ready at init | `buzzer_init()` returns `-ENODEV`; `main()` logs `LOG_WRN` and continues (shell still comes up; `buzzer` commands will report the device is unavailable). |
| `buzzer tone 0` or absurd frequency | `buzzer_tone` no-ops on 0; the shell command validates and rejects out-of-range input (e.g. > 20000 Hz) with a usage error. |

## Testing

- **Optional host unit test:** the `hz → half_period_us` conversion is pure
  (`1000000 / (2 * hz)`) and could get a tiny host test like `tilt.c`. It is nearly
  trivial; include only if cheap. No other pure logic exists.
- **Build verification:** clean pristine build for `nrf54l15dk/nrf54l15/cpuapp` with
  `--no-sysbuild`.
- **On-hardware verification:**
  1. `buzzer sweep` → eight rising tones, audibly stepping up, loudest near 4 kHz.
  2. `buzzer tone 4000` → a clear ~4 kHz tone (near resonance, should be loudest).
  3. `buzzer tone 800` and `buzzer tone 8000` → audible but weaker at the band edges.
  4. The pin/buzzer is silent (idle LOW) between tones and after playback.

## Files touched

| File | Change |
|------|--------|
| `boards/phial-common.dtsi` | Add `buzzer-gpios` to the existing `zephyr,user` node |
| `app/buzzer-test/CMakeLists.txt` | New |
| `app/buzzer-test/prj.conf` | New |
| `app/buzzer-test/sample.yaml` | New |
| `app/buzzer-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | New — includes phial-common.dtsi |
| `app/buzzer-test/src/buzzer.h` | New — `buzzer_init`, `buzzer_tone`, `buzzer_sweep` |
| `app/buzzer-test/src/buzzer.c` | New — GPIO toggle, tone/sweep, `buzzer` shell cmd |
| `app/buzzer-test/src/main.c` | New — `buzzer_init()` + idle |
