/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "zephyr/sys/atomic.h"
#include "zephyr/sys/reboot.h"
#include <zephyr/sys/printk.h>

#include <stdint.h>
#include <time.h>

#include "common.h"
#include "gatt.h"
#include "rb_tree.h"
#include "ukf.h"

#define ONE_MIN         60000
#define CONN_TIMEOUT_MS 30000

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

    struct cmd_ble_packet packet = {0};
    int64_t last_both_connected = k_uptime_get();
    int64_t prev = last_both_connected;

    while (1) {
        int64_t current = k_uptime_get();

        rb_lock();
        struct helm_node *nodeA = get_rb_node(0);
        struct helm_node *nodeB = get_rb_node(1);
        int both_connected = nodeA && nodeB && nodeA->connection_status && nodeB->connection_status;
        rb_unlock();

        if (both_connected) {
            last_both_connected = current;
        } else if ((current - last_both_connected) > CONN_TIMEOUT_MS) {
            goto reboot;
        }

        if (atomic_get(&is_time_set) && (current - prev) > ONE_MIN) {
            prev = current;
            packet.cmd = 1;
            struct timespec tp;
            clock_gettime(CLOCK_REALTIME, &tp);
            packet.time = tp.tv_sec;
        } else {
            packet.cmd = 0;
            packet.time = 0;
        }

        send_sync_pulse_to_helms((uint8_t *)&packet, sizeof(struct cmd_ble_packet));
        k_msleep(PULSE_DELAY);
    }

    return (0);

reboot:
    // sys_reboot doesn't return
    sys_reboot(SYS_REBOOT_WARM);
    return (-1);
}
