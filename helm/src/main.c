/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "battery.h"
#include "gatt.h"
#include "imu.h"
#include "magnet.h"

#define STACK_SIZE 1024
#define PRIORITY   7

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("CSSE4011 Project %s Chip\r\n", DEVICE_NAME);

    if (initialise_magnet_sensor()) {
        return -1;
    }

    if (initialise_imu()) {
        return -1;
    }

    if (initialise_bat_charge()) {
        return -1;
    }

    if (initialise_helm_gatt()) {
        return -1;
    }

    printk("[INFO] Network initialization complete\n");

    // The below will be deleted later.

    const char *const data = "Goodbye";
    while (1) {
        send_nus_temp(data);
    }
}

static void sensor_readout_thread(void *, void *, void *)
{
    int64_t magnet_time_ms = 0;
    double battery_voltage = 0;
    struct imu_data imu_data = {0};

    while (1) {
        k_msgq_get(&magnet_time_q, &magnet_time_ms, K_NO_WAIT);
        int64_t current_time_ms = k_uptime_get();

        k_msgq_get(&imu_q, &imu_data, K_NO_WAIT);

        if (get_battery_voltage(&battery_voltage) == 0) {
            printk("Time since last magnet: %lldms | gyro: %d deg/s | accel: %f m/s^2 | battery: "
                   "%.3fV\n",
                   current_time_ms - magnet_time_ms, imu_data.gyro_deg, imu_data.accel_ms2,
                   battery_voltage);

            k_msleep(500);
        }
    }
}

K_THREAD_DEFINE(sensor_readout, STACK_SIZE, sensor_readout_thread, NULL, NULL, NULL, PRIORITY, 0,
                5000);
