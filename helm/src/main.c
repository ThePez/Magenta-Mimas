/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

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
        return -1;
    }

    if (initialise_imu()) {
        return -1;
    }

    if (initialise_helm_gatt()) {
        return -1;
    }

    printk("[INFO] Mobile node initialization complete\n");
}
