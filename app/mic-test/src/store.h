/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_STORE_H_
#define PHIAL_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>

/* The mic_clip partition. */
#define MIC_CLIP_PARTITION   mic_clip_partition
#define MIC_CLIP_SIZE        FIXED_PARTITION_SIZE(MIC_CLIP_PARTITION)

/* Largest clip (in samples) whose WAV (44-byte header + 16-bit-mono PCM) fits
 * the partition. Rounded down to a multiple of 8 samples so the PCM byte count
 * is a multiple of the 16-byte flash write block. */
#define MIC_MAX_SAMPLES      ((((MIC_CLIP_SIZE) - 44U) / 2U) & ~7U)

struct clip_info {
    uint32_t addr;     /* absolute flash address of the WAV (partition base) */
    uint32_t len;      /* total bytes written (44 header + PCM, pre-padding)  */
    uint32_t samples;  /* PCM sample count                                    */
    uint32_t rate;     /* sample rate stamped in the WAV header               */
    bool     valid;    /* false until a successful save                       */
};

/* Write a WAV (header + `samples` 16-bit PCM samples from `pcm`) to the mic_clip
 * partition in one pass. Returns 0 on success, <0 on a flash error. */
int  store_save_wav(const int16_t *pcm, size_t samples, uint32_t rate);

/* Copy the last successfully stored clip's info (or .valid = false). */
void store_last(struct clip_info *out);

/* Erase the partition and clear the stored clip info. Returns 0 / <0. */
int  store_erase(void);

#endif /* PHIAL_STORE_H_ */
