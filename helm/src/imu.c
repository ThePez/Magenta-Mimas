/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "imu.h"

#include "common.h"

#include "zephyr/kernel.h"
#include "zephyr/sys/atomic.h"
#include "zephyr/sys/printk.h"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>

#define SAMPLING_FREQ 416
#define GYRO_RANGE    1000

/* ********************************************************************************************* */
/* Constants                                                                                     */
/* ********************************************************************************************* */

static const struct device *const lsm6dsl_dev = DEVICE_DT_GET_ONE(st_lsm6dsl);

/* ********************************************************************************************* */
/* State                                                                                         */
/* ********************************************************************************************* */

/* Message queue of depth 1 - we are only interested in the last item. */
K_MSGQ_DEFINE(imu_q, sizeof(struct imu_data), 1, 1);

static atomic_t pm_enable = ATOMIC_INIT(0);

/* ********************************************************************************************* */
/* Functions                                                                                     */
/* ********************************************************************************************* */

static void lsm6dsl_trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
    struct sensor_value accel_x;
    struct sensor_value gyro_z;

    sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
    sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);

    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel_x);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &gyro_z);

    // Either ms since boot or current internet time
    int64_t current = get_time();
    struct imu_data data = {.gyro_rads = sensor_value_to_double(&gyro_z),
                            .accel_ms2 = sensor_value_to_double(&accel_x),
                            .timestamp = current};

    // We are only interested in the last item.
    while (k_msgq_put(&imu_q, &data, K_NO_WAIT) != 0) {
        k_msgq_purge(&imu_q);
    }
}

int initialise_imu(void)
{
    if (!device_is_ready(lsm6dsl_dev)) {
        printk("[ERROR] LSM6DSL: device not ready\n");
        return (-ENODEV);
    }

    if (pm_device_runtime_enable(lsm6dsl_dev) < 0) {
        printk("[WARN] LSM6DSL: runtime pm init fail\n");
    } else {
        atomic_set(&pm_enable, 1);
    }

    // Only soft initialise the imu here. Just really checking that the device exists
    return (0);
}

int susspend_imu(void)
{
    if (!atomic_get(&pm_enable)) {
        return (-ENOTSUP);
    }

    int ret = pm_device_runtime_put(lsm6dsl_dev);
    if (ret < 0) {
        printk("[ERROR] LSM6DSL: IMU suspend failed\n");
        return (ret);
    }

    printk("[INFO] LSM6DSL: IMU suspended\n");
    return (0);
}

int resume_imu(void)
{
    // Set sampling frequency to 416Hz.
    const struct sensor_value frequency_attr = {.val1 = SAMPLING_FREQ, .val2 = 0};
    const struct sensor_trigger trig = {.type = SENSOR_TRIG_DATA_READY,
                                        .chan = SENSOR_CHAN_ACCEL_XYZ};
    struct sensor_value gyro_fs_attr;

    // Safe to call when not enabled -> results in a NO-OP
    int ret = pm_device_runtime_get(lsm6dsl_dev);
    if (ret < 0) {
        printk("[ERROR] LSM6DSL: runtime get fail\n");
        return (ret);
    }

    // Actually initialise the IMU
    ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                          &frequency_attr);
    if (ret < 0) {
        printk("[ERROR] Cannot set sampling frequency for accelerometer (%d)\n", ret);
        return (ret);
    }

    ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                          &frequency_attr);
    if (ret < 0) {
        printk("[ERROR] Cannot set sampling frequency for gyroscope (%d)\n", ret);
        return (ret);
    }

    sensor_degrees_to_rad(GYRO_RANGE, &gyro_fs_attr);
    ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &gyro_fs_attr);
    if (ret < 0) {
        printk("[ERROR] Cannot set gyroscope full-scale range (%d)\n", ret);
        return (ret);
    }

    ret = sensor_trigger_set(lsm6dsl_dev, &trig, lsm6dsl_trigger_handler);
    if (ret != 0) {
        printk("[ERROR] Could not set sensor type and channel (%d)\n", ret);
        return (ret);
    }

    ret = sensor_sample_fetch(lsm6dsl_dev);
    if (ret < 0) {
        printk("[ERROR] Sensor sample update error\n (%d)", ret);
        return (ret);
    }

    printk("[INFO] LSM6DSL: IMU resumed\n");
    return (0);
}
