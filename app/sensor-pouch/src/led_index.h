/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_LED_INDEX_H_
#define PHIAL_LED_INDEX_H_

#include <stdbool.h>
#include <stdint.h>

#define NUM_LEDS 16

/* Decode a cloud "LED" setting value to a 0-based LED index. Returns true and
 * sets *out_idx for values 1..16; returns false (leaving *out_idx untouched)
 * for anything else. */
bool led_index_decode(int32_t setting_value, uint8_t *out_idx);

#endif /* PHIAL_LED_INDEX_H_ */
