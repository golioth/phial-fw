/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "store.h"
#include "wav.h"

LOG_MODULE_REGISTER(store, LOG_LEVEL_INF);

#define FA_ID            FIXED_PARTITION_ID(MIC_CLIP_PARTITION)
#define ERASE_BLOCK      4096U     /* RRAM erase-block-size */
#define WRITE_ALIGN      16U       /* RRAM write-block-size */
#define CHUNK            256U      /* staging chunk; multiple of WRITE_ALIGN */

static struct clip_info last;

static uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1U) & ~(a - 1U);
}

int store_save_wav(const int16_t *pcm, size_t samples, uint32_t rate)
{
    const struct flash_area *fa;
    uint8_t hdr[WAV_HEADER_BYTES];
    uint8_t chunk[CHUNK];
    int rc;

    const uint32_t pcm_bytes = (uint32_t)samples * 2U;
    const uint32_t total     = WAV_HEADER_BYTES + pcm_bytes;     /* logical len */
    const uint32_t padded    = align_up(total, WRITE_ALIGN);     /* write size  */
    const uint32_t erased    = align_up(padded, ERASE_BLOCK);    /* erase size  */

    if (padded > MIC_CLIP_SIZE) {
        LOG_ERR("clip too large: %u > %u", padded, (uint32_t)MIC_CLIP_SIZE);
        return -ENOSPC;
    }

    rc = flash_area_open(FA_ID, &fa);
    if (rc < 0) {
        LOG_ERR("flash_area_open failed (%d)", rc);
        return rc;
    }

    rc = flash_area_erase(fa, 0, erased);
    if (rc < 0) {
        LOG_ERR("flash_area_erase failed (%d)", rc);
        goto out;
    }

    wav_header(hdr, (uint32_t)samples, rate);

    /* Stream header+PCM as one logical byte run, written in CHUNK-sized,
     * 16-byte-aligned pieces. The last chunk is zero-padded to WRITE_ALIGN. */
    for (uint32_t off = 0; off < padded; off += CHUNK) {
        uint32_t n = (padded - off < CHUNK) ? (padded - off) : CHUNK;

        for (uint32_t i = 0; i < n; i++) {
            uint32_t pos = off + i;        /* byte index into the logical WAV */

            if (pos < WAV_HEADER_BYTES) {
                chunk[i] = hdr[pos];
            } else if (pos < total) {
                chunk[i] = ((const uint8_t *)pcm)[pos - WAV_HEADER_BYTES];
            } else {
                chunk[i] = 0;              /* alignment padding */
            }
        }

        rc = flash_area_write(fa, off, chunk, n);
        if (rc < 0) {
            LOG_ERR("flash_area_write @%u failed (%d)", off, rc);
            goto out;
        }
    }

    /* Publish as one compound-literal store so the shell thread (which reads
     * `last` via store_last) can't observe valid=true alongside stale fields. */
    last = (struct clip_info){
        .addr    = (uint32_t)fa->fa_off,   /* absolute RRAM address */
        .len     = total,
        .samples = (uint32_t)samples,
        .rate    = rate,
        .valid   = true,
    };
    rc = 0;

out:
    flash_area_close(fa);
    return rc;
}

void store_last(struct clip_info *out)
{
    *out = last;
}

int store_erase(void)
{
    const struct flash_area *fa;
    int rc = flash_area_open(FA_ID, &fa);

    if (rc < 0) {
        return rc;
    }
    rc = flash_area_erase(fa, 0, MIC_CLIP_SIZE);
    flash_area_close(fa);
    if (rc == 0) {
        memset(&last, 0, sizeof(last));
    }
    return rc;
}

/* ---- mic shell command -------------------------------------------------- */

static int cmd_mic_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    struct clip_info c;

    store_last(&c);
    if (!c.valid) {
        shell_print(sh, "no clip stored yet (hold the boot button to record)");
        return 0;
    }
    shell_print(sh, "clip: %u bytes WAV @ 0x%06x  (%u samples, %u Hz, %u ms)",
                c.len, c.addr, c.samples, c.rate,
                (c.rate ? (c.samples * 1000U / c.rate) : 0U));
    shell_print(sh, "read off with:  nrfjprog --memrd 0x%06x --n %u  > rec.wav",
                c.addr, c.len);
    shell_print(sh, "(confirm the exact nRF54L RRAM read syntax for your tool)");
    return 0;
}

static int cmd_mic_erase(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    int rc = store_erase();

    if (rc < 0) {
        shell_error(sh, "erase failed (%d)", rc);
        return rc;
    }
    shell_print(sh, "mic_clip erased");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mic_sub,
    SHELL_CMD(info,  NULL, "Show the last stored clip + readback command.", cmd_mic_info),
    SHELL_CMD(erase, NULL, "Erase the stored clip.", cmd_mic_erase),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(mic, &mic_sub, "Phial mic clip storage", NULL);
