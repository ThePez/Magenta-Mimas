/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery.h"

#include "common.h"

#include "gatt.h"
#include "imu.h"
#include "magnet.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>

#define STACK_SIZE        2048
#define PRIORITY          3
#define THREAD_DELAY      (5 * 1000)
#define BAT_PACKET_PERIOD (9 * 1000)

#ifdef CHIP_A
#define NODE_NUM 0
#else
#define NODE_NUM 1
#endif

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

static struct ble_packet bat_packet = {
    .packet_id = BATTERY, .node_num = NODE_NUM, .crc16 = 0, .data = {{0}}};
static struct ble_packet sensor_packet = {
    .packet_id = SENSOR, .node_num = NODE_NUM, .crc16 = 0, .data = {{0}}};

/* ========================================================================== */
/* Send data packet for sensor data & battery voltage                         */
/* ========================================================================== */

static void send_sensor_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    int64_t magnet_time_ms = 0;
    int64_t battery_time_ms = 0;

    while (1) {
        k_sem_take(&notif_sem, K_FOREVER);

        // Collect Hall Effect Data
        k_msgq_get(&magnet_time_q, &magnet_time_ms, K_NO_WAIT);
        int64_t current = k_uptime_get();

        sensor_packet.data.sensor.magnet_dt = (current - magnet_time_ms);
        // Collect Accelerometer & Gyro Data
        k_msgq_get(&imu_q, &(sensor_packet.data.sensor.imu_data), K_NO_WAIT);

        sensor_packet.crc16 =
            crc16_ansi((char *)&(sensor_packet.data.sensor), sizeof(struct sensor_packet));

        // Send to Base
        send_data_nus(&sensor_packet, sizeof(struct ble_packet));

        // Send Battery Data every BAT_PACKET_PERIOD
        if ((current - battery_time_ms) < BAT_PACKET_PERIOD) {
            continue;
        }

        battery_time_ms = current;
        if (get_battery_charge(&(bat_packet.data.bat.bat_charge_pc)) == 0) {
            bat_packet.crc16 =
                crc16_ansi((char *)&(sensor_packet.data.bat), sizeof(struct bat_packet));
            send_data_nus(&bat_packet, sizeof(struct ble_packet));
        }
    }
}

K_THREAD_DEFINE(send_sensor, STACK_SIZE, send_sensor_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
