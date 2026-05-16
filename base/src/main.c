/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "zephyr/sys/reboot.h"
#include <zephyr/sys/printk.h>

#include "gatt.h"
#include "common.h"
#include <stdint.h>

/* ========================================================================== */
/* ENTRY POINT                                                                */
/* Initialise BT then kick off step 1. Everything else is                     */
/* event driven from the BT stack callbacks.                                  */
/* ========================================================================== */
int main(void)
{
    printk("[INFO] CSSE4011 Project Base Chip\r\n");

    int err = initialise_base_gatt();
    if (err < 0) {
        goto reboot;
    }

    char *data = "Base says hi";
    struct sensor_packet control;
    int64_t prev = 0;
    while (1) {
        int64_t current = k_uptime_get();
        if (current - prev > 1000) {
            send_sync_pulse_to_helms(data, strlen(data));
            prev = current;
        }

        if (k_msgq_get(&sensor_msg_queue, &control, K_NO_WAIT) == 0) {
            printk("Node ID: Helm-%c\n", control.node_num ? 'B' : 'A');
            printk("acceleration: %f\n", control.imu_data.accel_ms2);
            printk("gyroscope: %f\n", control.imu_data.gyro_rads);
        }

        k_msleep(10);
    }

    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
