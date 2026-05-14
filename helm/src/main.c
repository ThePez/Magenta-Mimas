/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "gatt.h"
#include "imu.h"
#include "magnet.h"

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("CSSE4011 Project %s Chip\r\n", DEVICE_NAME);

    if (initialise_magnet_sensor()) {
        goto reboot;
    }

    if (initialise_imu()) {
        goto reboot;
    }

    if (initialise_helm_gatt()) {
        goto reboot;
    }

    printk("[INFO] Mobile node initialization complete\n");
    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
