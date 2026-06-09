/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "mic.h"

LOG_MODULE_REGISTER(mic, LOG_LEVEL_INF);

#define PDM_NODE        DT_NODELABEL(pdm20)

/* ~10 ms PCM block: small enough that polling keep_going() between reads gives
 * ~10 ms button-release latency; big enough to keep DMA churn low. */
#define BLOCK_MS        10U
#define BLOCK_SAMPLES   (MIC_SAMPLE_RATE / 100U * (BLOCK_MS / 10U))   /* 160 */
#define BLOCK_BYTES     (BLOCK_SAMPLES * sizeof(int16_t))            /* 320 */
#define BLOCK_COUNT     8U
#define READ_TIMEOUT_MS 200

/* Driver-owned slab the PDM DMA fills; we copy out and free each block. */
K_MEM_SLAB_DEFINE_STATIC(mic_slab, BLOCK_BYTES, BLOCK_COUNT, 4);

static const struct device *const mic_dev = DEVICE_DT_GET(PDM_NODE);
static bool init_ok;

int mic_init(void)
{
    if (!device_is_ready(mic_dev)) {
        LOG_ERR("PDM device (pdm20) not ready");
        return -ENODEV;
    }

    struct pcm_stream_cfg stream = {
        .pcm_width = 16,
        .pcm_rate  = MIC_SAMPLE_RATE,
        .block_size = BLOCK_BYTES,
        .mem_slab  = &mic_slab,
    };
    struct dmic_cfg cfg = {
        .io = {
            /* Allow the PDM clock range that yields ~16 kHz (1.024 MHz) — the
             * MP34DT05 normal mode. */
            .min_pdm_clk_freq = 1000000,
            .max_pdm_clk_freq = 3250000,
            .min_pdm_clk_dc   = 40,
            .max_pdm_clk_dc   = 60,
        },
        .streams = &stream,
        .channel = {
            .req_num_streams = 1,
            .req_num_chan    = 1,
            .req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
        },
    };

    int rc = dmic_configure(mic_dev, &cfg);
    if (rc < 0) {
        LOG_ERR("dmic_configure failed (%d)", rc);
        return rc;
    }

    init_ok = true;
    return 0;
}

int mic_capture(int16_t *buf, size_t max_samples,
                bool (*keep_going)(void), size_t *out_samples)
{
    size_t got = 0;
    int rc = 0;

    *out_samples = 0;
    if (!init_ok) {
        return -ENODEV;
    }

    rc = dmic_trigger(mic_dev, DMIC_TRIGGER_START);
    if (rc < 0) {
        LOG_ERR("DMIC START failed (%d)", rc);
        return rc;
    }

    while (got < max_samples && keep_going()) {
        void *blk;
        size_t size;

        rc = dmic_read(mic_dev, 0, &blk, &size, READ_TIMEOUT_MS);
        if (rc < 0) {
            LOG_WRN("dmic_read failed (%d); stopping with %zu samples", rc, got);
            break;
        }

        size_t avail = size / sizeof(int16_t);
        size_t room  = max_samples - got;
        size_t take  = avail < room ? avail : room;

        memcpy(&buf[got], blk, take * sizeof(int16_t));
        got += take;
        k_mem_slab_free(&mic_slab, blk);
    }

    int stop_rc = dmic_trigger(mic_dev, DMIC_TRIGGER_STOP);
    if (stop_rc < 0) {
        LOG_WRN("DMIC STOP failed (%d)", stop_rc);
    }

    *out_samples = got;
    /* A clean stop (keep_going false / buffer full) is success; only a read
     * error propagates as failure. */
    return (rc < 0) ? rc : 0;
}
