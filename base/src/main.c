/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "zephyr/sys/reboot.h"
#include <zephyr/sys/printk.h>

#include <stdint.h>

#include "gatt.h"
#include "rb_tree.h"

/* ========================================================================== */
/* ENTRY POINT                                                                */
/* Initialise BT then kick off step 1. Everything else is                     */
/* event driven from the BT stack callbacks.                                  */
/* ========================================================================== */
int main(void)
{
    printk("[INFO] CSSE4011 Project Base Chip\r\n");

    // Wait for the rb_tree to initialise
    if (k_sem_take(&rb_semaphore, K_SECONDS(5)) < 0) {
        goto reboot;
    }

    int err = initialise_base_gatt();
    if (err < 0) {
        goto reboot;
    }

    char *data = "PULSE";
    while (1) {
        send_sync_pulse_to_helms(data, strlen(data));
        k_msleep(250);
    }

    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
