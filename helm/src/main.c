/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "gatt.h"
#include "imu.h"

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("CSSE4011 Project %s Chip\r\n", DEVICE_NAME);

    if (initialise_imu()) {
        goto reboot;
    }

    if (initialise_helm_gatt()) {
        goto reboot;
    }

    printk("[INFO] Mobile node initialization complete\n");

    while (1) {
        k_sleep(K_SECONDS(30));
        if (!am_i_connected()) {
            goto reboot;
        }
    }
    
    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
