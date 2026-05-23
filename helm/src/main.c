/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "gatt.h"
#include "imu.h"
#include <stdint.h>

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("[INFO] CSSE4011 Project %s Chip\r\n", DEVICE_NAME);

    if (initialise_imu()) {
        goto reboot;
    }

    if (initialise_helm_gatt()) {
        goto reboot;
    }

    printk("[INFO] Mobile node initialization complete\n");
    uint8_t timeout = 0;
    while (1) {
        (am_i_connected()) ? timeout = 0 : timeout++;
        if (timeout >= 30) {
            goto reboot;
        }

        k_sleep(K_SECONDS(1));
    }
    
    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
