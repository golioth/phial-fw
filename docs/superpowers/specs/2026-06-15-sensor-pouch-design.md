# Design: `app/sensor-pouch` — BME280 → Golioth over pouch (BLE GATT), LED from a cloud setting

- **Date:** 2026-06-15
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only) — **this app uses sysbuild +
  MCUboot** (for OTA), unlike every other app in the repo, which build `--no-sysbuild`.
- **Depends on:** the shared `boards/phial-common.dtsi` (16-LED `gpio-leds` map, BME280 on
  `i2c30`, boot button P1.12 in `zephyr,user`) and a new **pouch** west module.
- **Follows:** the upstream pouch BLE GATT example
  (`github.com/golioth/pouch` → `examples/zephyr/ble_gatt`).

## Goal

A new app that turns the Phial into a **pouch BLE-GATT device**: it samples the BME280 and,
when the **boot button (P1.12)** is pressed, advertises and requests a gateway; a pouch
broker/gateway then connects and **syncs** — uploading the latest BME280 reading as a
Golioth **Stream** datapoint and delivering a Golioth **Settings** value named `LED`
(integer **1-16**) that lights the corresponding board LED. A `sensorpouch status` shell
command reports device state.

The Phial is the pouch **device**; reaching the Golioth cloud requires a separate pouch
**broker/gateway** (phone app or another nRF) to bridge BLE↔internet. Building/operating
that gateway is out of scope — this spec is the device firmware.

## What pouch provides (researched, as of pouch `main`, 2026-06-15)

- pouch is a Zephyr module added via `west.yml`. Its in-tree **`golioth_sdk`** layer supports
  **Logging, OTA, Settings, and Stream** (State and RPC are **not** supported — neither is
  needed here).
- **Stream (uplink):** inside a `POUCH_UPLINK_HANDLER(fn)` (invoked on each sync), call
  `pouch_uplink_entry_write(".s/<path>", POUCH_CONTENT_TYPE_JSON, data, len, POUCH_FOREVER)`.
  The `.s/` path prefix routes to the Golioth Stream service.
- **Settings (downlink):** `GOLIOTH_SETTINGS_HANDLER(LED, cb)` where `cb` is
  `int (*)(int32_t new_value)`; the macro infers the INT type via `_Generic`. The setting
  name (`LED`) must match the key configured in Golioth.
- **Init:** `pouch_init(&config)` with `config.certificate` (DER) + `config.private_key`
  (a PSA key id). The example loads both from a filesystem.
- **BLE transport:** `<pouch/transport/bluetooth/gatt.h>`; the example wraps it in
  `ble_peripheral_{init,start,request_gateway,button_handler}()`. `request_gateway(true)`
  sets a "please sync me" flag in the advertisement.
- **Events:** `<pouch/events.h>` exposes session lifecycle events (used to clear the
  gateway-request flag after a sync completes).

## Key integration decisions & risks

### NCS version (the primary risk)

pouch is verified on **NCS v3.2.3**; this workspace is on **NCS v3.1.1** (and golioth
firmware-SDK `v0.21.0`). Adding the pouch module at `revision: main` may hit Kconfig/API
drift. **Mitigation:** the first implementation milestone is a **spike** that proves pouch
compiles/links and BLE comes up on our NCS 3.1.1 *before* any feature work. If it fails, we
pin pouch to a compatible revision, or (worst case, escalated to the user) bump NCS.

### sysbuild + MCUboot (OTA) vs the repo norm

OTA was requested, so this app builds with **sysbuild + MCUboot** (`sysbuild.conf` →
`SB_CONFIG_BOOTLOADER_MCUBOOT=y`) — the only such app in the repo. Build command:

```bash
west build -p -b nrf54l15dk/nrf54l15/cpuapp --sysbuild app/sensor-pouch
```

### Partition layout (MCUboot swap vs the shared dtsi)

MCUboot image swap needs **`slot0` and `slot1` equal-sized** (both 664 KB, the stock
`nrf54l15_partition.dtsi` layout). The shared `phial-common.dtsi` shrank `slot1` to 536 KB
and added a 128 KB `mic_clip` partition (for mic-test). So `sensor-pouch`'s overlay must, on
top of the include:
- **restore `slot1_partition`** to `<0xb6000 DT_SIZE_K(664)>`, and
- **`/delete-node/ &mic_clip_partition;`** (sensor-pouch doesn't use it).

Resulting map = the stock layout: `mcuboot` 64 KB · `slot0` 664 KB · `slot1` 664 KB ·
`storage` 36 KB. The 36 KB `storage_partition` is mounted as **LittleFS** (`/lfs1`) for
credentials.

### Credentials / provisioning (LittleFS + MCUmgr, like the example)

pouch authenticates with per-device Golioth **PKI** (secp384r1). `credentials.c` (ported
from the example) reads a DER cert + key from `/lfs1/credentials/{crt.der,key.der}` and
imports the key into PSA. Provisioning is done over serial with MCUmgr/smpmgr:

```bash
mcumgr --conntype serial --connstring $PORT fs upload crt.der /lfs1/credentials/crt.der
mcumgr --conntype serial --connstring $PORT fs upload key.der /lfs1/credentials/key.der
# then reboot
```

The `/lfs1/credentials/` directory is created on first boot. A first-boot LittleFS "can't
mount; formatting" warning is expected. **PSA / secp384r1 on the nRF54L's CRACEN** is a
bring-up item validated during the spike.

### west / module wiring

Add to `phial-app/west.yml` (alongside the existing `golioth` project):
```yaml
- name: pouch
  path: deps/modules/lib/pouch
  revision: main          # pin to a tag/SHA once the spike confirms it builds
  url: https://github.com/golioth/pouch.git
```
Then `west update` and `pip install -r deps/modules/lib/pouch/requirements.txt` (pouch's
zcbor codegen tooling). The existing `golioth` (classic firmware-SDK) project **stays** — it
imports NCS into the workspace and is load-bearing; pouch's `golioth_sdk` is a separate
layer and is what this app uses. We do **not** import pouch's own `west-*.yml` (they pin a
different NCS); adding the module is enough for its `zephyr/module.yml` to be picked up.

## Non-goals

- **No gateway/broker firmware.** Device side only.
- **No State or RPC services** (pouch's golioth_sdk doesn't support them).
- **No local LED override / extra shell commands** beyond `status` (only `status` was
  requested). The settings→LED code path is the same a future `led <n>` command would call.
- **No authenticated BLE OOB pairing.** Just-Works pairing as in the example
  (`CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y`); the boot button is repurposed from the
  example's OOB-auth role to "request a sync."
- **No change to the other apps** or to `phial-common.dtsi` (partition fixes live in this
  app's overlay).

## Architecture

New app `app/sensor-pouch`. Modules, each with one responsibility:

| Unit | Responsibility | Origin |
|------|----------------|--------|
| `main.c` | Mount LFS; load creds; `pouch_init()`; register `POUCH_UPLINK_HANDLER`; init LEDs + boot button (interrupt) → `ble_peripheral_request_gateway(true)`; clear the flag on session-end event; BLE up; register `sensorpouch status`. | New (glue) |
| `ble_peripheral.c/h` | BLE GATT peripheral: advertise, gateway-request flag, connection mgmt. | Ported from example |
| `credentials.c/h` | Load DER cert + key from `/lfs1/credentials/` into PSA. | Ported from example |
| `env.c/h` | BME280 sample thread (~10 s) + mutex-guarded latest-reading cache; `env_get()`. | Ported from `app/sensor-test/src/env.c` |
| `app_settings.c/h` | `GOLIOTH_SETTINGS_HANDLER(LED, cb)` (int32) → clamp 1-16 → light LED N via the LED subsystem (1 on, others off); `app_settings_led_index()` accessor for `status`. | New |
| `led_index.c/h` | Pure helper: validate/decode a setting value (1-16) → 0-based LED index or "invalid". Unit-tested. | New |
| `fw_update.c`, `fatal_error.c` | OTA component + fatal-error handler. | Ported from example |

### Public interfaces (new code)

```c
/* led_index.h — pure, host-testable */
#define NUM_LEDS 16
/* Decode a cloud setting value to a 0-based LED index. Returns true and sets
 * *out_idx for 1..16; returns false (leaving *out_idx untouched) otherwise. */
bool led_index_decode(int32_t setting_value, uint8_t *out_idx);

/* env.h (ported) */
int  env_init(void);                 /* start sampling thread; <0 if BME280 absent */
bool env_get(struct env_reading *out); /* latest cached reading; false if none yet */

/* app_settings.h */
void app_settings_init(const struct device *leds);  /* remember LED device */
int  app_settings_led_index(void);   /* current 1-based index, or 0 if unset */
```

### Data flow

```
BME280 --(~10s)--> env.c cache
boot button press --> ble_peripheral_request_gateway(true) + advertise
   gateway connects --> pouch session sync:
       uplink:   POUCH_UPLINK_HANDLER -> pouch_uplink_entry_write(".s/...", JSON, latest reading)
       downlink: LED setting -> app_settings cb -> led_index_decode -> light LED N
   session end event --> request_gateway(false)  (back to idle)
sensorpouch status --> BLE state, creds loaded?, last sync result, LED index, latest reading
```

The BME280 stream entry is JSON, e.g. `{"temp":22.4,"humidity":41.0,"pressure":99.8}`
(units: °C / %RH / kPa, matching sensor-test's `env.c`).

## Runtime behavior

- **Boot:** mount `/lfs1`; load creds → PSA; `pouch_init()`; `env_init()`; LEDs + button;
  BLE up; **idle (no gateway requested)**. Missing creds → log "provision over MCUmgr then
  reboot" and stay up (shell + MCUmgr still work).
- **Button:** press → `request_gateway(true)` + advertise. On pouch session-end event →
  `request_gateway(false)`. A short log/LED cue marks sync start + result.
- **Settings:** `LED` int → clamp 1-16 (out-of-range logged + ignored, prior kept) → light
  LED N, others off; cache index.
- **Shell:** `sensorpouch status` → advertising/connected, creds-loaded, last sync result,
  current LED index, latest BME280 reading.

## Error handling

| Condition | Handling |
|-----------|----------|
| Credentials missing/invalid | `pouch_init` skipped or fails; `LOG_ERR` + "reprovision over MCUmgr"; device stays up (shell + MCUmgr live). |
| BME280 absent / not ready | `env_init()` returns <0; sampling disabled; `env_get` returns false; uplink omits the reading; device runs. |
| BLE init failure | `LOG_ERR` and return (fatal, as in the example). |
| LED / button GPIO not ready | `LOG_WRN`, continue (button-less = sync only via a future shell cmd; LED-less = setting logged only). |
| `LED` setting out of 1-16 | Logged, ignored; previous LED state kept. |
| LittleFS first-boot format warning | Expected; documented; ignored. |

## Testing

- **Spike (critical first gate):** add the pouch module + `pip install` its requirements;
  build the upstream `ble_gatt` example (or a minimal pouch+BLE skeleton) for
  `nrf54l15dk/nrf54l15/cpuapp` with `--sysbuild` to prove pouch compiles/links and BLE comes
  up on our **NCS 3.1.1**. Gate the rest of the work on this.
- **Host unit test:** `led_index_decode` (1-16 → index; reject 0, 17, negatives) — a
  self-contained `cc` test like `app/sensor-test/tests/tilt/test_tilt.c`.
- **Build verification:** clean `--sysbuild` build for `nrf54l15dk/nrf54l15/cpuapp`.
- **On-hardware (user):** provision creds via MCUmgr; press button → advertise + (with a
  gateway) a Stream datapoint appears in Golioth; set the `LED` setting (1-16) in Golioth →
  the matching LED lights; `sensorpouch status` reflects state.

## Files touched

| File | Change |
|------|--------|
| `phial-app/west.yml` | Add the `pouch` module project |
| `app/sensor-pouch/CMakeLists.txt` | New |
| `app/sensor-pouch/prj.conf` | New (BT/SMP, pouch, golioth settings+OTA, LittleFS, MCUmgr, sensor/BME280, LED, shell, mbedTLS heap) |
| `app/sensor-pouch/sysbuild.conf` | New — `SB_CONFIG_BOOTLOADER_MCUBOOT=y` |
| `app/sensor-pouch/sample.yaml` | New |
| `app/sensor-pouch/README.md` | New — build (`--sysbuild`), provisioning, Golioth `LED` setting, usage |
| `app/sensor-pouch/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | New — include phial-common.dtsi; restore slot1; delete mic_clip; LittleFS `/lfs1` on storage |
| `app/sensor-pouch/src/main.c` | New |
| `app/sensor-pouch/src/ble_peripheral.{c,h}` | New (ported) |
| `app/sensor-pouch/src/credentials.{c,h}` | New (ported) |
| `app/sensor-pouch/src/env.{c,h}` | New (ported from sensor-test) |
| `app/sensor-pouch/src/app_settings.{c,h}` | New |
| `app/sensor-pouch/src/led_index.{c,h}` | New (pure, unit-tested) |
| `app/sensor-pouch/src/fw_update.c`, `fatal_error.c` | New (ported) |
| `app/sensor-pouch/tests/led_index/test_led_index.c` | New — host unit test |
| `README.md` (repo top-level) | Add `sensor-pouch` row (note: sysbuild/MCUboot app) |
