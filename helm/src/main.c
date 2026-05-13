/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "battery.h"
#include "gatt.h"
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

    if (initialise_bat_charge()) {
        return -1;
    }

    if (initialise_helm_gatt()) {
        return -1;
    }

    printk("[INFO] Network initialization complete\n");

    // The below will be deleted later.

    const char *const data = "Goodbye";
    while (1) {
        send_nus_temp(data);
    }
}
