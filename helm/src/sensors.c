/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery.h"
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

struct bat_packet {
    char node_num;
    int32_t bat_charge_pc;

    uint16_t crc16;
};

struct sensor_packet {
    char node_num;
    struct imu_data imu_data;
    uint64_t magnet_dt;

    uint16_t crc16;
};

/* ========================================================================== */
/* Send data packet for battery voltage                                       */
/* ========================================================================== */

static struct bat_packet bat_packet = {.node_num = NODE_NUM, .bat_charge_pc = 0, .crc16 = 0};
static struct sensor_packet sensor_packet = {
    .node_num = NODE_NUM, .imu_data = {0}, .magnet_dt = 0, .crc16 = 0};

// This is somewhat temporary
static void send_bat_pak_thread(void *, void *, void *)
{
    while (1) {
        if (get_battery_charge(&(bat_packet.bat_charge_pc)) == 0) {
            bat_packet.crc16 = 0;
            bat_packet.crc16 = crc16_ansi((char*) &bat_packet, sizeof(struct bat_packet));

            send_data_nus(&bat_packet, sizeof(struct bat_packet));
        }

        k_msleep(BAT_PACKET_PERIOD);
    }
}

// Create thread with some delay
K_THREAD_DEFINE(send_bat_pak, STACK_SIZE, send_bat_pak_thread, NULL, NULL, NULL, PRIORITY, 0,
                THREAD_DELAY);

/* ========================================================================== */
/* Send data packet for sensor data                                           */
/* ========================================================================== */

static void send_sensor_thread(void *, void *, void *)
{
    int64_t magnet_time_ms = 0;

    while (1) {
        k_sem_take(&notif_sem, K_FOREVER);

        k_msgq_get(&magnet_time_q, &magnet_time_ms, K_NO_WAIT);
        sensor_packet.magnet_dt = (k_uptime_get() - magnet_time_ms);

        k_msgq_get(&imu_q, &(sensor_packet.imu_data), K_NO_WAIT);

        sensor_packet.crc16 = 0;
        sensor_packet.crc16 = crc16_ansi((char*) &sensor_packet, sizeof(struct sensor_packet));

        send_data_nus(&sensor_packet, sizeof(struct sensor_packet));
    }
}

K_THREAD_DEFINE(send_sensor, STACK_SIZE, send_sensor_thread, NULL, NULL, NULL, PRIORITY, 0,
                THREAD_DELAY);
