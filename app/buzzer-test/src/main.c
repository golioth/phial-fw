/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buzzer.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    if (buzzer_init() != 0) {
        LOG_WRN("buzzer disabled (GPIO unavailable)");
    }
    LOG_INF("Phial buzzer-test: try 'buzzer sweep' or 'buzzer tone <hz> [ms]'");
    return 0;
}
