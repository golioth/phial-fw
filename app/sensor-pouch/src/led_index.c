/* SPDX-License-Identifier: Apache-2.0 */
#include "led_index.h"

bool led_index_decode(int32_t setting_value, uint8_t *out_idx)
{
    if (setting_value < 1 || setting_value > NUM_LEDS) {
        return false;
    }
    *out_idx = (uint8_t)(setting_value - 1);
    return true;
}
