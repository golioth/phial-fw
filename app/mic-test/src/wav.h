/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_WAV_H_
#define PHIAL_WAV_H_

#include <stdint.h>

/* Number of bytes a canonical PCM WAV header occupies. */
#define WAV_HEADER_BYTES 44

/* Fill `hdr` with a 44-byte little-endian canonical PCM WAV/RIFF header for a
 * 16-bit, mono stream of `samples` samples at `rate` Hz. Writes exactly
 * WAV_HEADER_BYTES bytes. Pure function — no hardware, no allocation. */
void wav_header(uint8_t hdr[WAV_HEADER_BYTES], uint32_t samples, uint32_t rate);

#endif /* PHIAL_WAV_H_ */
