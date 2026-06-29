/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "translation.h"
#include "usb_hid.h"
#include "rb_tree.h"
#include "json.h"

#include <zephyr/kernel.h>
#include <math.h>

#define BAT_UPDATE_MS 45000 // 45 seconds
#define JSON_UPDATE_MS 60000 // 60 seconds
#define KALMAN_WAIT    K_MSEC(10)
#define STACKSIZE      2048
#define PRIORITY       6

K_MSGQ_DEFINE(trans_queue, sizeof(struct json_packet), 5, 4);

void thread_trans(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int64_t prev = 0;
    int64_t prev_bat = 0;

    int32_t charge = -1;

    // Gyro Speed and direction
    double speed = 0;
    int8_t direction = 1;

    while (1) {
        if (k_sem_take(&sensor_semaphore, K_MSEC(10)) == 0) {
            rb_lock();
            struct helm_node *node = get_rb_node(0);
            speed = node->imu_data.gyro_rads;
            node = NULL;
            rb_unlock();

            direction = (speed < 0) ? -1 : 1;
            // Translate into keyboard press
            enum hid_kbd_code key = translate_into_button(fabs(speed), direction);
            // Pass to HID controller
            if (key != HID_KEY_G) {
                k_msgq_put(&hid_key_msgq, &key, K_NO_WAIT);
            }
        }

        int64_t current_bat = k_uptime_get();
        if (current_bat - prev_bat > BAT_UPDATE_MS && charge > 0) {
            prev_bat = current_bat;

            // Pass the battery % to HID thread
            enum hid_kbd_code key = translate_bat_to_button(charge);
            k_msgq_put(&hid_key_msgq, &key, K_NO_WAIT);
        }

        // Only send JSON every 60 seconds -> Build JSON packet for PC script
        int64_t current = k_uptime_get();
        if (current - prev > JSON_UPDATE_MS) {
            prev = current;

            struct json_packet packet = {0};

            // Grab tree stuff
            rb_lock();

            struct helm_node *node = get_rb_node(0);        
            packet.nodeA.bat = node->battery_data;
            packet.nodeA.imu = node->imu_data;
            packet.nodeA.id = node->id;
            packet.nodeA.connection_status = node->connection_status;
            node = NULL;

            rb_unlock();

            // Update most recent battery %
            charge = packet.nodeA.bat.bat_charge;

            // timestamp the creation of this json_packet
            packet.time = get_time();

            // Fill in remaining items
            packet.direction = direction;
            packet.speed = speed;
            k_msgq_put(&trans_queue, &packet, K_NO_WAIT);
        }
    }
}

K_THREAD_DEFINE(trans_thread, STACKSIZE, thread_trans, NULL, NULL, NULL, PRIORITY, 0, 0);
