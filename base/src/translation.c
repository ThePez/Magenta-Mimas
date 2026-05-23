/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "translation.h"

#include "ukf.h"
#include "usb_hid.h"
#include "rb_tree.h"
#include "json.h"

#include <zephyr/kernel.h>

#define JSON_UPDATE_MS 5000
#define KALMAN_WAIT    K_MSEC(10)
#define STACKSIZE      2048
#define PRIORITY       6

K_MSGQ_DEFINE(trans_queue, sizeof(struct json_packet), 5, 4);

void thread_trans(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct kalman data = {0};
    struct helm_node *nodeA;
    struct helm_node *nodeB;
    struct json_packet packet = {0};

    int64_t prev = 0;

    while (1) {
        // Grab data from kalman
        if (k_msgq_get(&kalman_msgq, &data, KALMAN_WAIT) == 0) {
            // Translate into keyboard press
            enum hid_kbd_code key = translate_into_button(data.magntidue, data.direction);
            // Pass to HID controller
            if (key != HID_KEY_G) {
                k_msgq_put(&hid_key_msgq, &key, K_NO_WAIT);
            }
        }
        
        // Only send JSON every 5 seconds -> Build JSON packet for PC script
        int64_t current = k_uptime_get();
        if (current - prev < JSON_UPDATE_MS) {
            continue;
        }

        prev = current;

        // Grab tree stuff
        rb_lock();

        // Helm A
        nodeA = get_rb_node(0);
        packet.nodeA.bat = nodeA->battery_data;
        packet.nodeA.imu = nodeA->imu_data;
        packet.nodeA.id = nodeA->id;
        packet.nodeA.connection_status = nodeA->connection_status;
        // Helm B
        nodeB = get_rb_node(1);
        packet.nodeB.bat = nodeB->battery_data;
        packet.nodeB.imu = nodeB->imu_data;
        packet.nodeB.id = nodeB->id;
        packet.nodeB.connection_status = nodeB->connection_status;

        rb_unlock();

        // timestamp the creation of this json_packet
        packet.time = get_time();

        // Fill in remaining items
        packet.direction = data.direction;
        packet.speed = data.magntidue;
        k_msgq_put(&trans_queue, &packet, K_NO_WAIT);
    }
}

K_THREAD_DEFINE(trans_thread, STACKSIZE, thread_trans, NULL, NULL, NULL, PRIORITY, 0, 0);
