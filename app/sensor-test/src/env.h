/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHIAL_ENV_H_
#define PHIAL_ENV_H_

#include <stdbool.h>
#include <stdint.h>

/* Latest BME280 environmental reading. Units match what the Zephyr BME280
 * driver reports: temperature in °C, humidity in %RH, pressure in kPa. */
struct env_reading {
    float   temp_c;
    float   humidity_pct;
    float   pressure_kpa;
    int64_t uptime_ms;   /* k_uptime_get() when this sample was taken */
    bool    valid;       /* false until the first successful sample */
};

/* Verify the BME280 is ready and start the 10 s sampling thread.
 * Returns 0 on success, -ENODEV if the device is not ready (thread NOT
 * started, so a missing sensor produces no recurring warnings). */
int env_init(void);

/* Copy the latest cached reading into *out under the module mutex.
 * Returns true if a valid sample exists, false otherwise. This is the seam a
 * future BLE/Golioth transport reads — it never touches the bus or blocks. */
bool env_get(struct env_reading *out);

#endif /* PHIAL_ENV_H_ */
