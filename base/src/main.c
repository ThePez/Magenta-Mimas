/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gatt.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>

// Fatal warning message on startup
static const char *const warn_message =
    "Big RIP. Nothing works now, power cycle the chip to try again";

/* ========================================================================== */
/* ENTRY POINT                                                                */
/* Initialise BT then kick off step 1. Everything else is                     */
/* event driven from the BT stack callbacks.                                  */
/* ========================================================================== */
int main(void)
{
    printk("[INFO] CSSE4011 Project Base Chip\r\n");

    if (set_static_address()) {
        return (-1);
    }

    int err = bt_enable(NULL);
    if (err) {
        printk("[ERROR] Bluetooth init failed (err %d)\n%s\r\n", err, warn_message);
        return (err);
    }

    printk("[INFO] Bluetooth initialized\r\n");

    err = start_scan(); /* STEP 1 for GATT process */
    if (err) {
        printk("Initial scan failed (err %d)\n%s\n", err, warn_message);
        return (err);
    }

    char *data = "Base says hi";

    while (1) {
        send_sync_pulse_to_helms(data, strlen(data));
        k_msleep(1000);
    }

    return (0);
}
