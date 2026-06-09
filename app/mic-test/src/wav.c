/* SPDX-License-Identifier: Apache-2.0 */

#include "wav.h"

/* Little-endian stores — the WAV format is little-endian regardless of host. */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void put_tag(uint8_t *p, const char *tag)
{
    p[0] = (uint8_t)tag[0];
    p[1] = (uint8_t)tag[1];
    p[2] = (uint8_t)tag[2];
    p[3] = (uint8_t)tag[3];
}

void wav_header(uint8_t hdr[WAV_HEADER_BYTES], uint32_t samples, uint32_t rate)
{
    const uint32_t data_bytes = samples * 2U;   /* 16-bit mono: 2 bytes/sample */

    put_tag(&hdr[0],  "RIFF");
    put_u32(&hdr[4],  36U + data_bytes);
    put_tag(&hdr[8],  "WAVE");
    put_tag(&hdr[12], "fmt ");
    put_u32(&hdr[16], 16U);            /* PCM fmt chunk size */
    put_u16(&hdr[20], 1U);             /* audio format = PCM */
    put_u16(&hdr[22], 1U);             /* channels = mono */
    put_u32(&hdr[24], rate);
    put_u32(&hdr[28], rate * 2U);      /* byte rate = rate * blockalign */
    put_u16(&hdr[32], 2U);             /* block align = channels * 2 */
    put_u16(&hdr[34], 16U);            /* bits per sample */
    put_tag(&hdr[36], "data");
    put_u32(&hdr[40], data_bytes);
}
