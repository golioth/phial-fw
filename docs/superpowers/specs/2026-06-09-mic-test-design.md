# Design: `app/mic-test` — MP34DT05 PDM mic, button-gated capture to flash

- **Date:** 2026-06-09
- **Status:** Approved design (pre-implementation)
- **Board target:** `nrf54l15dk/nrf54l15/cpuapp` (secure-only, no MCUboot, no sysbuild)
- **Depends on:** the shared `boards/phial-common.dtsi` (16-LED `gpio-leds` map, the
  `zephyr,user` node) and `conf/shell-common.conf`. New app, sibling to `app/led-test`,
  `app/sensor-test`, and `app/buzzer-test`.

## Goal

A new app that records a short audio clip from the Phial's onboard **MP34DT05TR-A**
PDM MEMS microphone while a button is held, stores it to internal flash as a WAV file,
and reports where it landed so it can be pulled off the board with a J-Link memory read
and played back.

Behavior: hold the **"boot" button (P1.12)** → all 16 LEDs light solid and the mic
records; release → LEDs off, the clip is written to flash, and the console prints the
clip's flash address + length + the exact `nrfjprog`/JLink readback command. Recording
is capped at **~4 s** (buffer full ends it early). This is mic bring-up — first use of a
PDM mic on this board.

## Hardware

- **Mic:** MP34DT05TR-A — STMicro PDM (Pulse Density Modulation) MEMS microphone, 1-bit
  serial output clocked by the host. MIC_CLK = **P1.05**, MIC_DATA = **P1.04**. Both are
  in the nRF54L peripheral GPIO domain (P1), so they are reachable by `pdm20` (peripheral
  domain, 0xd0000 block — same family as the PWM instances). No power-domain surprise here
  (unlike the buzzer, where PWM could not reach P0).
- **PDM peripheral:** `pdm20` (`nordic,nrf-pdm`) exists in the nRF54L15 SoC dtsi but ships
  `status = "disabled"`; we enable it with a pinctrl group. The Zephyr DMIC API
  (`<zephyr/audio/dmic.h>`) + the `dmic_nrfx_pdm` driver
  (`CONFIG_AUDIO_DMIC` / `CONFIG_AUDIO_DMIC_NRFX_PDM`) drive it.
- **Button:** the "boot" button on **P1.12** (not one of the four DK buttons, which were on
  P1.13/P1.09/P1.08/P0.04 and are `/delete-node/`'d in `phial-common.dtsi`). Active-low
  (pressed → GND), configured as input with an internal pull-up — polarity confirmed on
  this board.

### Audio format (16 kHz, normal mic mode)

- **Format:** 16 kHz sample rate, 16-bit signed PCM, mono (left channel only).
- **Sizing:** 16 kHz × 2 bytes = **32 KB/s**; 4 s ≈ **128 KB**. Drives a 128 KB RAM capture
  buffer and a 128 KB flash partition. 128 KB is ~68 % of the 188 KB `cpuapp_sram`; the
  remaining ~60 KB covers stacks, the shell/log buffers, the DMIC driver slab, and RTT
  buffers. Implementation must confirm the image still links (if RAM is tight, the buffer
  is the one knob — shrink it / the clip cap).
- **PDM clock:** the DMIC driver derives the PDM clock from the requested PCM rate
  (rate × decimation 64). 16 kHz → ~**1.024 MHz** PDM clock, squarely in the MP34DT05's
  **normal clock mode** (~1.0–3.25 MHz) for full SNR. The app sets
  `dmic_cfg.io.min_pdm_clk_freq`/`max_pdm_clk_freq` to roughly 1.0–3.25 MHz so the driver
  lands near 1.024 MHz. The **actual** rate the driver settles on is read back from
  `dmic_configure()` and used for the WAV header — we do not hard-assume exactly 16000 Hz.

## Non-goals

- **No BLE / Golioth transport.** Retrieval is J-Link memory read only. Network upload is
  later work; this app proves capture + storage.
- **No filesystem.** Raw WAV bytes to a fixed partition; no LittleFS/mount.
- **No live audio processing** (no VU meter, FFT, compression, or playback on-device).
- **No streaming-to-flash.** Capture lands in RAM first, flushed once on release
  (see Architecture / rejected alternatives).
- **No multi-clip management.** One clip slot; a new recording overwrites the previous.
- **No DFU / MCUboot.** Standalone secure-only image; this is why the `slot1` RRAM region
  is free for the clip store (see Flash partition).

## Capture flow (chosen approach: capture-to-RAM, flush-on-release)

While the button is held, the DMIC streams PCM blocks into a preallocated 128 KB RAM
buffer (linear, bounded — stops at full ≈ 4 s); all 16 LEDs are solid on. On release the
DMIC stops, a WAV header is prepended, and the whole buffer is written to the flash
partition in one pass. Flash is written exactly once, when nothing is time-critical.

**Rejected — stream directly to flash:** write each DMIC block to RRAM as it arrives.
RRAM write latency + write-block/erase semantics risk not keeping up with the 32 KB/s
stream (dropped samples), for more complexity and no benefit at this clip size.

**Rejected — capture to RAM, read RAM over J-Link (skip flash):** simpler, but ignores
the explicit "store to flash" requirement and RAM is volatile (clobbered on reset).

## Architecture

Five units, each with one responsibility, mirroring the repo's module style:

1. **`mic.c` / `mic.h` — the DMIC module.** Owns the `pdm20` device. Configures it and
   runs a bounded capture into a caller-provided PCM buffer. Knows nothing about flash,
   LEDs, or the button. The capture loop polls a caller-supplied predicate between DMIC
   blocks so it can be stopped externally without `mic.c` knowing *why*.
2. **`store.c` / `store.h` — the flash store.** Owns the `mic_clip` partition. Writes a
   WAV (header + PCM) in one pass and reports where it landed. Knows nothing about the mic.
3. **`wav.c` / `wav.h` — pure WAV header builder.** Fills a 44-byte canonical PCM
   WAV/RIFF header for a given sample count + rate. No hardware; the one unit-tested piece.
4. **`main.c` — orchestrator.** Owns the button GPIO (P1.12) and the 16 LEDs, runs the
   press→capture→release→store state machine, owns the 128 KB capture buffer (static), and
   registers the `mic` shell command. The only writer of the LED device (no concurrency).
5. **Devicetree** — enable `pdm20` w/ pinctrl, add `button-gpios` to `zephyr,user`, add the
   `mic_clip` flash partition.

The 128 KB capture buffer is a `static int16_t[65536]` in `main.c`, passed by pointer into
`mic_capture()`, so `mic.c` is allocation-free and the buffer size is owned in one place.
Place it in `.noinit` (Zephyr's `__noinit` macro) so it is neither zero-initialized at
boot nor counted as initialized data — it is fully overwritten by each capture anyway.
The implementation should report the actual `bss`/`noinit` figures from the link map so
the ~60 KB headroom is a measured number, not an estimate.

### Public interfaces

```c
/* mic.h */
int  mic_init(void);                       /* configure pdm20; <0 on failure */
/* Capture up to max_samples 16-bit mono samples into buf. Blocks; between DMIC
 * blocks (~10 ms) calls keep_going() and stops when it returns false or the
 * buffer is full. *out_samples = samples actually captured; *out_rate = the
 * actual PCM rate the driver settled on. Returns 0 on success, <0 on error. */
int  mic_capture(int16_t *buf, size_t max_samples,
                 bool (*keep_going)(void),
                 size_t *out_samples, uint32_t *out_rate);

/* store.h */
struct clip_info {
    uint32_t addr;     /* absolute flash address of the WAV (partition base) */
    uint32_t len;      /* total bytes written (44 header + PCM)              */
    uint32_t samples;  /* PCM sample count                                   */
    uint32_t rate;     /* sample rate in the WAV header                      */
    bool     valid;    /* false until a successful save                      */
};
int  store_save_wav(const int16_t *pcm, size_t samples, uint32_t rate);
void store_last(struct clip_info *out);    /* last successful clip, or .valid=false */
int  store_erase(void);                    /* wipe the partition; clears clip_info  */

/* wav.h */
void wav_header(uint8_t hdr[44], uint32_t samples, uint32_t rate); /* 16-bit mono */
```

### Devicetree changes (`boards/phial-common.dtsi`)

Enable the PDM peripheral and route its pins:

```dts
&pinctrl {
    /omit-if-no-ref/ pdm20_default: pdm20_default {
        group1 {
            psels = <NRF_PSEL(PDM_CLK, 1, 5)>,   /* MIC_CLK  P1.05 */
                    <NRF_PSEL(PDM_DIN, 1, 4)>;    /* MIC_DATA P1.04 */
        };
    };
};

&pdm20 {
    status = "okay";
    pinctrl-0 = <&pdm20_default>;
    pinctrl-names = "default";
    clock-source = "PCLK32M_HFXO";   /* crystal-backed clock, low jitter */
};
```

Add the button to the existing `zephyr,user` node (do not create a second one — it
already holds `mag-gpios` and `buzzer-gpios`):

```dts
/ {
    zephyr,user {
        mag-gpios    = <&gpio1 13 GPIO_ACTIVE_LOW>;   /* existing */
        buzzer-gpios = <&gpio0  4 GPIO_ACTIVE_HIGH>;  /* existing */
        button-gpios = <&gpio1 12 GPIO_ACTIVE_LOW>;   /* new: "boot" button P1.12 */
    };
};
```

`main.c` resolves it with `GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_gpios)` and
configures `GPIO_INPUT | GPIO_PULL_UP`. The `pdm20` node is inert for apps that do not
enable `CONFIG_AUDIO_DMIC` (led/sensor/buzzer-test), so it is safe in the shared dtsi —
same precedent as the accel/BME280 nodes.

### Flash partition (`mic_clip`)

The existing `storage` partition is only 36 KB; we need ~128 KB. Add a dedicated
`mic_clip` fixed-partition in the RRAM region MCUboot would use for **`slot1` (image-1)**.
This app runs secure-only with **no MCUboot and no DFU**, so slot1 (664 KB @ `0xb6000`) is
never used and is free for app data. Carve its tail:

```dts
&cpuapp_rram {
    partitions {
        mic_clip_partition: partition@13c000 {
            label = "mic_clip";
            reg = <0x13c000 DT_SIZE_K(128)>;   /* 0x13c000..0x15c000 */
        };
    };
};
```

`0x13c000 + 0x20000 = 0x15c000`, exactly where the `storage` partition begins, and clear
of `cpuflpr_rram` (0x165000) — no overlap with anything *in use*. It sits inside the
*declared* slot1 range (0xb6000..0x15c000); that is intentional and safe given no MCUboot.
(Alternative, noted but not chosen: shrink slot1 in an overlay so nothing overlaps the
declared region. The plain add is simpler and the overlap is inert.)

`store.c` references it by label via `FIXED_PARTITION_ID(mic_clip_partition)` /
`flash_area_open()`.

### `store_save_wav` sequence (one pass on release)

1. `flash_area_open(FIXED_PARTITION_ID(mic_clip_partition), &fa)`.
2. `flash_area_erase(fa, 0, <rounded size>)` — RRAM is written through the flash API;
   erase first to honor write-block semantics (RRAM `write-block-size` is 16 bytes).
3. `wav_header(hdr, samples, rate)`; `flash_area_write(fa, 0, hdr, 44)`; then write the PCM
   immediately after, chunked to the flash write-block-size with end-padding as required by
   the alignment rules.
4. Populate `clip_info` (absolute addr = partition base, len = 44 + pcm_bytes, samples,
   rate, valid = true) for `store_last()`.

Storing the WAV header inline means the J-Link readback is a directly-playable `.wav` — no
host-side assembly.

### Kconfig / build (`app/mic-test/`)

- `CMakeLists.txt` — pulls in `../../conf/shell-common.conf` via `EXTRA_CONF_FILE`,
  `project(mic_test)`, sources `src/main.c`, `src/mic.c`, `src/store.c`, `src/wav.c`.
- `prj.conf` — `CONFIG_GPIO=y`, `CONFIG_AUDIO=y`, `CONFIG_AUDIO_DMIC=y`,
  `CONFIG_AUDIO_DMIC_NRFX_PDM=y`, `CONFIG_FLASH=y`, `CONFIG_FLASH_MAP=y`; the RTT/UART
  console block mirroring the sibling apps. Does **not** enable `CONFIG_SENSOR`, so the
  shared LIS2DH node stays inert here; `CONFIG_BME280=n` is set so the BME280 driver (which
  is `default y` whenever its dtsi node is present) is not pulled into this audio app.
- `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` — `#include`s `../../../boards/phial-common.dtsi`.
- `sample.yaml` — `build_only`, tags `phial` / `mic` / `shell`.
- `README.md` — top-level app README (see below).

## Orchestration (`main.c`)

Polled state machine (~10 ms tick; the DMIC read loop *is* the busy phase, so no extra
thread is needed — and `main` is the sole LED writer, so no LED concurrency):

```
Idle:           LEDs off; poll button.
Press edge:     all 16 LEDs on; mic_capture(buf, 65536, button_still_pressed,
                                             &n, &rate) — blocks until release or full.
Release / full: LEDs off; if n > 0: store_save_wav(buf, n, rate);
                log one-line summary + the nrfjprog/JLink readback command; -> Idle.
```

`button_still_pressed()` is a small file-scope predicate reading the button GPIO; passed to
`mic_capture` so `mic.c` stays button-agnostic. Block period (~10 ms) is the
release-detection latency — imperceptible.

### Shell command (`mic`)

| Command | Behavior |
|---------|----------|
| `mic info` | Print the last clip's flash address, length, sample count, duration, and the ready-to-paste readback command (e.g. `nrfjprog --memrd <addr> --n <len>`). Reports "no clip" if none. |
| `mic erase` | `store_erase()` — wipe the partition and clear `clip_info`. |

Recording itself is button-driven, not a shell command (YAGNI).

## App README (`app/mic-test/README.md`)

A short top-level README so the demo is self-documenting, covering:
- **What it does** — hold the boot button to record from the PDM mic; release to store a
  WAV to flash.
- **Hardware** — MP34DT05TR-A, MIC_CLK P1.05 / MIC_DATA P1.04, boot button P1.12; 16 kHz /
  16-bit mono, ~4 s max.
- **Build & flash** — the standard `west build -p -b nrf54l15dk/nrf54l15/cpuapp
  --no-sysbuild app/mic-test` and flash, consistent with the sibling apps.
- **Record** — hold the button (LEDs all on while recording), release to stop.
- **Retrieve** — copy the `nrfjprog`/JLink command the console prints (clip address +
  length) to read the `mic_clip` partition off the board into a `.wav` and play it; note
  the exact nRF54L RRAM read syntax is to be confirmed during bring-up.
- **Shell** — `mic info` / `mic erase`.

## Error handling

| Condition | Handling |
|-----------|----------|
| DMIC not ready / `dmic_configure` fails at init | `mic_init()` returns <0; `main` logs `LOG_ERR`; `mic` command reports the mic unavailable. Button + LEDs still function (LEDs light on press; no capture occurs). |
| `dmic_read()` error mid-capture | Stop capture, keep whatever was captured, return what we have with a `LOG_WRN`. |
| Buffer fills before release | Stop at ~4 s, `LOG_INF` "max length reached," store normally. |
| `flash_area_*` erase/write error | `store_save_wav` returns <0; `main` logs `LOG_ERR`; `clip_info` not updated; any previous clip untouched. |
| Button never pressed | Idle indefinitely; nothing logged. |

## Testing

- **Host unit test** for `wav.c` (`wav_header`): assert the RIFF/WAVE/fmt /data tag bytes,
  the chunk sizes, sample rate, byte-rate (`rate*2`), block-align (2), bits-per-sample (16),
  and `data` size (`samples*2`) for a couple of sample-count/rate inputs. Self-contained
  `cc`-compiled host test under `app/mic-test/tests/wav/test_wav.c`, mirroring
  `app/sensor-test/tests/tilt/test_tilt.c` (native_sim is Linux-only; this runs on macOS
  too). The only pure logic worth isolating.
- **Build verification:** clean pristine `--no-sysbuild` build for
  `nrf54l15dk/nrf54l15/cpuapp`; confirm the image links with the 128 KB static buffer.
- **On-hardware:**
  1. Hold the boot button → all 16 LEDs light solid.
  2. Speak; release → console logs clip address / length / duration + the readback command;
     LEDs go off.
  3. Run the printed `nrfjprog --memrd` (or JLink `savebin`) command, save to `rec.wav`,
     and confirm it plays back recognizable audio.
  4. `mic info` reports the same address/length/duration.
  5. `mic erase`, then `mic info` reports no clip.
  6. Hold > 4 s → "max length reached" logged, a ~4 s clip still stores and plays.

## Files touched

| File | Change |
|------|--------|
| `boards/phial-common.dtsi` | Enable `pdm20` + pinctrl; add `button-gpios` to `zephyr,user`; add `mic_clip_partition` |
| `app/mic-test/CMakeLists.txt` | New |
| `app/mic-test/prj.conf` | New |
| `app/mic-test/sample.yaml` | New |
| `app/mic-test/README.md` | New — app overview, build, record, retrieve |
| `app/mic-test/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | New — includes phial-common.dtsi |
| `app/mic-test/src/mic.h` / `mic.c` | New — DMIC config + bounded capture |
| `app/mic-test/src/store.h` / `store.c` | New — WAV write to flash + clip info |
| `app/mic-test/src/wav.h` / `wav.c` | New — pure WAV header builder (unit-tested) |
| `app/mic-test/src/main.c` | New — button/LED state machine + `mic` shell cmd |
| `app/mic-test/tests/wav/test_wav.c` | New — `wav_header` host unit test |
