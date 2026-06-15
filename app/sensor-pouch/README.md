# app/sensor-pouch — BME280 sensor to Golioth via pouch BLE GATT

Reads the **BME280** temperature / humidity / pressure sensor and uploads the
latest reading to the **Golioth Stream** (path `.s/sensor`) as a JSON datapoint
`{"temp", "humidity", "pressure"}` — but only when a **pouch gateway/broker**
requests a sync.

This app is a **pouch BLE-GATT device**: it does not connect to the internet
directly. Reaching the Golioth cloud requires a separate pouch **gateway/broker**
(a phone running the pouch app, or another nRF running a pouch gateway image) to
bridge BLE↔internet. Press the boot button (P1.12) to start advertising and
request a sync from any nearby gateway; the gateway pulls the latest BME280
reading and pushes it to Golioth on the device's behalf.

A **Golioth Settings** value named `LED` (integer 1–16) controls which of the
board's 16 LEDs is lit. Set it in the Golioth console; the device applies it on
the next sync. Values outside 1–16 are ignored.

This is the repo's only **sysbuild / MCUboot** app, so it supports
over-the-air firmware updates.

## Hardware

| Signal    | nRF pin | Notes                                         |
|-----------|---------|-----------------------------------------------|
| BME280    | i2c30   | Address 0x76; sampled ~every 10 s             |
| Button    | P1.12   | "boot" button, active-low; press to advertise |
| LEDs 1–16 | GPIO    | One lights according to the `LED` cloud setting|

The BME280 is sampled continuously in the background. The **most recent** reading
is what gets uploaded when a gateway syncs the session — not a running average.

## Build & flash

This app uses **sysbuild** (MCUboot bootloader + application image built
together). The sysbuild sub-image build requires `pykwalify`, which lives in the
west workspace virtual environment. Activating the venv before building is
required; using `west` from `.venv/bin/west` alone is **not** sufficient.

```bash
# Activate the workspace venv first (adjust <workspace> to your checkout root,
# e.g. source ~/golioth/phial-fw/.venv/bin/activate)
source <workspace>/.venv/bin/activate

# From the west manifest dir (the phial-app repo root)
west build -p -b nrf54l15dk/nrf54l15/cpuapp --sysbuild app/sensor-pouch
west flash
```

> **Note:** all other apps in this repo use `--no-sysbuild`. This is the only
> one that requires `--sysbuild`.

## Provision credentials (required)

The pouch BLE-GATT transport authenticates with Golioth using **per-device PKI
credentials** (DER-encoded certificate + private key). These must be uploaded to
the device's LittleFS partition before it can connect through a gateway.

See [Golioth PKI documentation](https://docs.golioth.io/connectivity/credentials/pki)
for how to issue device certificates.

### Upload with MCUmgr

```bash
# Set PORT to your device's serial port, e.g. /dev/ttyACM0 or COM1
mcumgr --conntype serial --connstring $PORT fs upload crt.der /lfs1/credentials/crt.der
mcumgr --conntype serial --connstring $PORT fs upload key.der /lfs1/credentials/key.der
```

After uploading both files, reboot the device.

### Upload with SMP Manager (alternative)

```bash
smpmgr --port $PORT --mtu 128 file upload crt.der /lfs1/credentials/crt.der
smpmgr --port $PORT --mtu 128 file upload key.der /lfs1/credentials/key.der
```

After uploading both files, reboot the device.

> **First-boot LittleFS warning (expected):** on the very first boot after
> flashing, the filesystem has not been formatted yet and you will see:
>
> ```log
> <err> littlefs: lfs.c:1389: Corrupted dir pair at {0x0, 0x1}
> <wrn> littlefs: can't mount (LFS -84); formatting
> ```
>
> This is normal. The filesystem is formatted automatically and subsequent boots
> are clean.

## Use it

1. Ensure a **pouch gateway/broker** is running and reachable over BLE
   (phone app or another nRF acting as gateway).
2. Press the **boot button (P1.12)** to start advertising and trigger a sync.
3. The gateway bridges the session to Golioth; the latest BME280 reading appears
   in the Golioth **Stream** under `.s/sensor`:
   ```json
   {"temp": 23.4, "humidity": 48.2, "pressure": 101.3}
   ```
4. To light a specific LED, set the `LED` **Settings** value (integer 1–16) in
   the Golioth console. The device applies it on the next sync. Values outside
   1–16 are silently ignored.

> Note: the boot button *initiates* a sync. After that, the BLE peripheral layer
> (ported from the pouch example) also re-requests a gateway periodically while
> disconnected — every `CONFIG_EXAMPLE_SYNC_PERIOD_S` seconds (default 20). So the
> device keeps trying to sync after the first press, not strictly once-per-press.
> Raise that Kconfig (or gate the resync) if you want button-only behavior.

## Shell commands

| Command              | Action                                              |
|----------------------|-----------------------------------------------------|
| `sensorpouch status` | Show advertising/sync state, LED index, latest BME280 reading |
