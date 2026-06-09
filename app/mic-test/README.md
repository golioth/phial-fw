# app/mic-test — PDM microphone capture to flash

Records a short clip from the Phial's onboard **MP34DT05TR-A** PDM MEMS microphone
while the **boot button (P1.12)** is held — all 16 LEDs light while recording —
and on release stores the clip as a WAV to a dedicated flash partition, printing
the address + length so it can be pulled off with a J-Link memory read and played.

## Hardware

| Signal   | nRF pin | Notes                          |
|----------|---------|--------------------------------|
| MIC_CLK  | P1.05   | PDM clock (driven by `pdm20`)  |
| MIC_DATA | P1.04   | PDM data                       |
| Button   | P1.12   | "boot" button, active-low      |

Capture format: **16 kHz, 16-bit mono**, ~4 s max (128 KB `mic_clip` partition).

## Build & flash

```bash
# from the west manifest dir (the phial-app repo root)
west build -p -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild app/mic-test
west flash
```

Open the shell over RTT (or the J-Link VCOM UART — see the repo README).

## Record

Hold the boot button — the 16 LEDs light and the mic records. Release to stop
(or after ~4 s, whichever comes first). The console prints where the clip landed.

## Retrieve

`mic info` reprints the last clip's flash address, length, and a readback command,
e.g.:

```
mic info
clip: 128044 bytes WAV @ 0x13c000  (64000 samples, 16000 Hz, 4000 ms)
read off (binary) in a J-Link session:  savebin rec.wav 0x13c000 128044
```

Dump the `mic_clip` region into `rec.wav` and play it. Use a tool that writes
**raw binary**, e.g. J-Link's `savebin`:

```bash
JLinkExe -device nRF54L15_M33 -if SWD -speed 4000 -autoconnect 1
J-Link> savebin rec.wav 0x13c000 128044
```

Note: `nrfjprog --memrd` prints a **hex text** dump, not binary — redirecting it
to a file does NOT produce a playable WAV. Confirm the exact device name / read
syntax for your J-Link/`nrfutil` version; the printed address+length are the
inputs you need either way.

## Shell commands

| Command     | Action                                            |
|-------------|---------------------------------------------------|
| `mic info`  | Show the last stored clip + the readback command. |
| `mic erase` | Erase the stored clip.                            |
