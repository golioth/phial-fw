/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_MAG_H_
#define PHIAL_MAG_H_

#include <stdbool.h>

/* Configure the LF21115TMR magnet-switch GPIO (P1.13) as an input.
 * Returns 0 on success, -ENODEV if the GPIO is not ready (feature disabled). */
int mag_init(void);

/* True when a magnet is present. The LF21115TMR pulls its push-pull output low
 * in a field; the DT spec is GPIO_ACTIVE_LOW, so the logical pin value is 1 then.
 * This is the seam a future BLE/Golioth transport reads. */
bool mag_present(void);

#endif /* PHIAL_MAG_H_ */
