/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_BUZZER_H_
#define PHIAL_BUZZER_H_

#include <stdint.h>

/* Configure the buzzer GPIO (P0.04) as an output, idle LOW.
 * Returns 0 on success, -ENODEV if the GPIO is not ready. */
int buzzer_init(void);

/* Play a square-wave tone at `hz` for `ms` milliseconds (blocking).
 * No-op when hz == 0 or before a successful buzzer_init(). The pin is left
 * LOW afterward. */
void buzzer_tone(uint32_t hz, uint32_t ms);

/* Play the fixed half-octave sweep (800..8000 Hz), blocking. */
void buzzer_sweep(void);

#endif /* PHIAL_BUZZER_H_ */
