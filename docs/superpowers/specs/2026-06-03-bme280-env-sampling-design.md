# Design: `app/sensor-test` — BME280 environmental sampling

- **Date:** 2026-06-03
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild)
- **Depends on:** I²C bring-up (`i2c30`, SDA P0.00 / SCL P0.01), already in
  `boards/phial-common.dtsi`. Sibling to the existing LIS2DH12 tilt feature
  ([2026-06-02-sensor-test-tilt-led-design.md](2026-06-02-sensor-test-tilt-led-design.md)).

## Goal

Round out the `sensor-test` demo with **environmental data collection** from the
on-board BME280 (temperature, humidity, pressure). Readings print to the console
every 10 s and are available on demand via an `env` shell command. The data is
held in a module-level struct so a **future BLE/Golioth module can read it and
ship it upstream** — that downstream use is out of scope here.

The BME280 sits on the Phial sensor bus at I²C address `0x76` (see
[phial-app/README.md](../../../README.md) and
[2026-05-14-phial-bringup-design.md](2026-05-14-phial-bringup-design.md), which
parked the BME280 as a "Phase 2, once `i2c scan` confirms addresses" item). The
I²C controller is already up; this feature instantiates the sensor node and
adds the driver + sampling.

## Non-goals

- **No LED interaction.** The 50 Hz accel→LED control loop in `main.c` is not
  modified. Environmental data is console-only.
- **No BLE / Golioth transport.** This feature only collects and displays. The
  `env_get()` accessor is the seam the later transport work will build on.
- **No persistence / calibration tuning.** Unlike `tilt`, there is nothing to
  calibrate; the `env` command is read-only.

## Behavior

| Trigger | Output |
|---------|--------|
| Every 10 s (sampling thread) | One `LOG_INF` line: `env: T=23.45 C  RH=41.20 %  P=98.71 kPa` |
| `env` shell command | Prints the latest cached reading plus its age in ms, or `no sample yet` before the first successful read |
| Sensor fetch failure | `LOG_WRN`; last good reading retained; sampling continues |

Channels and units come straight from the Zephyr BME280 driver:

| Channel | Zephyr channel | Unit |
|---------|----------------|------|
| Temperature | `SENSOR_CHAN_AMBIENT_TEMP` | °C |
| Humidity | `SENSOR_CHAN_HUMIDITY` | %RH |
| Pressure | `SENSOR_CHAN_PRESS` | kPa |

## Architecture

Three changes, mirroring the structure the tilt feature already established
(self-contained sensor module + shared devicetree + minimal `main.c` glue):

1. **`env.c` / `env.h` — the environmental module.** Owns the BME280 device, a
   dedicated 10 s sampling thread, the latest-reading cache, and the `env` shell
   command. This is the only file that touches the BME280.
2. **`main.c` — one line of glue.** Calls `env_init()` at startup. The accel/LED
   loop is untouched.
3. **Devicetree** — the BME280 node, added once to the shared
   `boards/phial-common.dtsi` alongside the LIS2DH12.

Unlike `tilt.c` (pure angle math, no hardware), `env.c` is mostly hardware glue:
the Zephyr sensor API already returns calibrated values in real units, so there
is no pure transformation to factor out and unit-test. See **Testing** below.

### Public interface — `env.h`

```c
struct env_reading {
    float   temp_c;        /* °C  (SENSOR_CHAN_AMBIENT_TEMP) */
    float   humidity_pct;  /* %RH (SENSOR_CHAN_HUMIDITY)     */
    float   pressure_kpa;  /* kPa (SENSOR_CHAN_PRESS)        */
    int64_t uptime_ms;     /* k_uptime_get() at sample time  */
    bool    valid;         /* false until the first good sample */
};

/* Verify the BME280 is ready and start the sampling thread.
 * Returns 0 on success, -ENODEV if the device is not ready. */
int  env_init(void);

/* Copy the latest cached reading into *out (under the module mutex).
 * Returns true if a valid sample exists, false otherwise. */
bool env_get(struct env_reading *out);
```

`env_get()` is the seam the later BLE/Golioth module consumes — it takes a
snapshot under the lock and returns, so callers never touch the BME280 or block
on I²C.

### Concurrency

- A single static `struct env_reading` guarded by a `k_mutex`.
- **One writer**: the sampling thread, which fetches and updates under the lock.
- **Readers**: the `LOG_INF` line (logs its own local copy), the `env` shell
  command, and the future transport module — all via `env_get()`.
- The critical section is a struct copy only; the I²C transaction happens
  outside the lock so a reader never waits on the bus.

### Sampling thread

`K_THREAD_DEFINE` with a ~1 KB stack (sensor fetch + log formatting; `main`'s
stack is 2048, this does less). Loop:

```
loop:
  if sensor_sample_fetch(bme) == 0
     and channel_get TEMP/HUMIDITY/PRESS all succeed:
        lock; write cache (valid=true, uptime_ms=now); unlock
        LOG_INF the reading
  else:
        LOG_WRN("BME280 fetch failed") and keep the previous cache
  k_msleep(10000)
```

The 10 s interval is a compile-time constant (`ENV_SAMPLE_MS`). Not runtime
tunable — there is nothing to tune for a demo telemetry cadence, and a future
BLE feature will own its own send interval anyway.

### Devicetree change

Add to the shared `boards/phial-common.dtsi`, right after the `lis2dh12` node:

```dts
&i2c30 {
    bme280: bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
        status = "okay";
    };
};
```

Placed in the **shared** dtsi (not the app overlay) to match the LIS2DH12
precedent: an unbound sensor node is inert for apps that don't enable its
driver, so `led-test` is unaffected and the node is available to future apps.
`env.c` resolves it with `DEVICE_DT_GET(DT_NODELABEL(bme280))`.

The Zephyr BME280 driver matches `compatible = "bosch,bme280"` and
`CONFIG_BME280` depends on `DT_HAS_BOSCH_BME280_ENABLED` — a single compatible,
with none of the dual-compatible quirk the LIS2DH12 node needs.

### Kconfig and build

- `prj.conf`: add `CONFIG_BME280=y`. `CONFIG_SENSOR`, `CONFIG_I2C`,
  `CONFIG_FPU`, and `CONFIG_CBPRINTF_FP_SUPPORT` are already enabled.
- `CMakeLists.txt`: add `src/env.c` to `target_sources(app PRIVATE ...)`.

## Error handling

| Condition | Handling |
|-----------|----------|
| BME280 not ready at init | `env_init()` returns `-ENODEV`; `main()` logs `LOG_WRN` and continues so the LED demo still runs. Env is non-critical. |
| `sensor_sample_fetch` or `channel_get` fails at runtime | `LOG_WRN`, retain last cache, continue looping — no crash, no LED impact. |
| `env_get()` before first good sample | Returns `false`; the `env` command prints `no sample yet`. |

## Testing

- **No new host unit test.** The tilt feature unit-tests `tilt.c` because it is
  pure math. `env.c` has no equivalent pure logic — it is device glue over the
  Zephyr sensor API, which already returns calibrated real-world units. A unit
  test here would exercise the framework, not our code.
- **Build verification:** clean build for `nrf54l15dk/nrf54l15/cpuapp` with
  `--no-sysbuild` via the `.venv` west.
- **On-hardware verification:**
  1. Console shows an `env:` line every ~10 s.
  2. Values respond to stimulus (breath on the board raises RH/temp).
  3. `env` command returns the cached reading with a plausible age; returns
     `no sample yet` only in the first 10 s window.
  4. LED tilt behavior is unchanged (regression check that the accel loop was
     not disturbed).

## Files touched

| File | Change |
|------|--------|
| `boards/phial-common.dtsi` | Add `bme280@76` node |
| `app/sensor-test/prj.conf` | Add `CONFIG_BME280=y` |
| `app/sensor-test/CMakeLists.txt` | Add `src/env.c` to sources |
| `app/sensor-test/src/env.h` | New — `env_reading`, `env_init`, `env_get` |
| `app/sensor-test/src/env.c` | New — device, thread, cache, `env` shell cmd |
| `app/sensor-test/src/main.c` | Add `env_init()` call at startup |
