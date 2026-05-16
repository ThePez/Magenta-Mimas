/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

struct bat_packet {
    int32_t bat_charge_pc;
};

struct imu_data {
    double gyro_rads;
    double accel_ms2;
};

struct sensor_packet {
    struct imu_data imu_data;
    uint64_t magnet_dt;
};

union ble_data {
    struct bat_packet bat;
    struct sensor_packet sensor;
};

struct ble_packet {
    uint8_t packet_id;
    uint8_t node_num;
    uint16_t crc16;

    union ble_data data;
};

enum ble_packet_id {
    SENSOR = 0,
    BATTERY = 1
};

#endif
