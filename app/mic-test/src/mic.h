/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_MIC_H_
#define PHIAL_MIC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PCM format the mic captures. 16 kHz / 16-bit / mono. The PDM driver lands on
 * the closest achievable rate (<1 % from 16000) and does not report it back, so
 * MIC_SAMPLE_RATE is what we record AND what we stamp into the WAV header. */
#define MIC_SAMPLE_RATE 16000U

/* Configure the pdm20 PDM peripheral. Returns 0 on success, <0 on failure
 * (device not ready / configure error). */
int mic_init(void);

/* Capture up to `max_samples` 16-bit mono samples into `buf` (blocking). Polls
 * keep_going() once per DMIC block and stops when it returns false or the buffer
 * is full (worst-case stop latency ~one block + the read timeout, a few hundred
 * ms). On return *out_samples holds the number of samples captured.
 * Returns 0 on success (incl. a clean stop), <0 on a DMIC error (whatever was
 * captured so far is still reported in *out_samples). */
int mic_capture(int16_t *buf, size_t max_samples,
                bool (*keep_going)(void), size_t *out_samples);

#endif /* PHIAL_MIC_H_ */
