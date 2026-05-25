/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

#include "battery.h"
#include "gatt.h"
#include "imu.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>

#define BAT_PACKET_PERIOD_MS 9000
#define PRIORITY             3
#define STACK_SIZE           2048

#if (NODE_ID == 0)
#define NODE_NUM 0
#elif (NODE_ID == 1)
#define NODE_NUM 1
#endif

// Initialied here, extern'ed in common.h
atomic_t is_time_set = ATOMIC_INIT(0);

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

static struct ble_packet bat_packet = {.packet_id = BATTERY, .node_num = NODE_NUM, .crc16 = 0};
static struct ble_packet sensor_packet = {.packet_id = SENSOR, .node_num = NODE_NUM, .crc16 = 0};

/* ========================================================================== */
/* Send data packet for sensor data & battery voltage                         */
/* ========================================================================== */

static void send_sensor_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int64_t last_bat_reading = 0;
    double *mv = &bat_packet.data.bat.bat_mv;
    int32_t *charge = &bat_packet.data.bat.bat_charge;
    time_t *bat_ts = &bat_packet.data.bat.timestamp;

    struct imu_data *imu_data = &sensor_packet.data.imu;

    while (1) {
        k_sem_take(&notif_sem, K_FOREVER);

        // Collect IMU Data
        k_msgq_get(&imu_q, imu_data, K_NO_WAIT);
        sensor_packet.crc16 = crc16_ansi((char *)&(sensor_packet.data), sizeof(union ble_data));

        // Send to Base
        send_data_nus(&sensor_packet, sizeof(sensor_packet));

        // Send Battery Data every BAT_PACKET_PERIOD_MS
        int64_t now = k_uptime_get();
        if ((now - last_bat_reading) < BAT_PACKET_PERIOD_MS) {
            continue;
        }

        // Collect battery data
        last_bat_reading = now;
        *bat_ts = get_time();
        get_battery_charge(charge);
        get_battery_voltage(mv);

        bat_packet.crc16 = crc16_ansi((char *)&(bat_packet.data), sizeof(union ble_data));

        // Send to base
        send_data_nus(&bat_packet, sizeof(bat_packet));
    }
}

K_THREAD_DEFINE(send_sensor, STACK_SIZE, send_sensor_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
