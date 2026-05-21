/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/cdefs.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

extern atomic_t is_time_set;

static inline time_t get_time(void)
{
    if (atomic_get(&is_time_set)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (ts.tv_sec);
    }
    
    return k_uptime_get();
}

static inline void set_absolute_time(time_t time)
{
    struct timespec ts;
    ts.tv_sec = time;
    ts.tv_nsec = 0;
    clock_settime(CLOCK_REALTIME, &ts);
    atomic_set(&is_time_set, 1);
}

struct bat_packet {
    time_t timestamp;
    double bat_mv;
    int32_t bat_charge;
};

struct imu_data {
    time_t timestamp;
    double gyro_rads;
    double accel_ms2;
};

union ble_data {
    struct bat_packet bat;
    struct imu_data imu;
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

struct __packed cmd_ble_packet {
    uint8_t cmd;
    time_t time;
};

#endif
