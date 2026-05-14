/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "imu.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

#define SAMPLING_FREQ 416
#define GYRO_RANGE    1000

#define MUTEX_WAIT_MS 5
#define PRIORITY      7
#define STACK_SIZE    1024

K_MUTEX_DEFINE(imu_mutex);

/* ********************************************************************************************* */
/* Constants                                                                                     */
/* ********************************************************************************************* */

static const struct device *const lsm6dsl_dev = DEVICE_DT_GET_ONE(st_lsm6dsl);

/* ********************************************************************************************* */
/* State                                                                                         */
/* ********************************************************************************************* */

static struct sensor_value accel_x;
static struct sensor_value gyro_z;

/* ********************************************************************************************* */
/* Functions                                                                                     */
/* ********************************************************************************************* */

static void lsm6dsl_trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
    // accelerometer
    sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
    sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);

    if (k_mutex_lock(&imu_mutex, K_MSEC(MUTEX_WAIT_MS)) != 0) {
        printk("Mutex not unlocking!\n");
    }

    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel_x);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &gyro_z);

    k_mutex_unlock(&imu_mutex);
}

int initialise_imu(void)
{
    // Set sampling frequency to 416Hz.
    const struct sensor_value frequency_attr = {.val1 = SAMPLING_FREQ, .val2 = 0};
    const struct sensor_trigger trig = {.type = SENSOR_TRIG_DATA_READY,
                                        .chan = SENSOR_CHAN_ACCEL_XYZ};
    struct sensor_value gyro_fs_attr;

    if (!device_is_ready(lsm6dsl_dev)) {
        printk("LSM6DSL: device not ready.\n");
        return (-ENODEV);
    }

    int ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                              &frequency_attr);
    if (ret < 0) {
        printk("Error %d: Cannot set sampling frequency for accelerometer.\n", ret);
        return (ret);
    }

    ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
                          &frequency_attr);
    if (ret < 0) {
        printk("Error %d: Cannot set sampling frequency for gyroscope.\n", ret);
        return (ret);
    }

    sensor_degrees_to_rad(GYRO_RANGE, &gyro_fs_attr);
    ret = sensor_attr_set(lsm6dsl_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &gyro_fs_attr);
    if (ret < 0) {
        printk("Error %d: Cannot set gyroscope full-scale range.\n", ret);
        return (ret);
    }

    ret = sensor_trigger_set(lsm6dsl_dev, &trig, lsm6dsl_trigger_handler);
    if (ret != 0) {
        printk("Error %d: Could not set sensor type and channel\n", ret);
        return (ret);
    }

    ret = sensor_sample_fetch(lsm6dsl_dev);
    if (ret < 0) {
        printk("Error %d: Sensor sample update error\n", ret);
        return (ret);
    }

    return 0;
}

/* BELOW IS JUST FOR TESTING */
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static void sensor_readout_thread(void *, void *, void *)
{
    int32_t gyro_deg;
    double accel_double;

    while (1) {
        if (k_mutex_lock(&imu_mutex, K_MSEC(MUTEX_WAIT_MS)) != 0) {
            printk("Mutex not unlocking!\n");
        }

        gyro_deg = sensor_rad_to_degrees(&gyro_z);
        accel_double = sensor_value_to_double(&accel_x);

        k_mutex_unlock(&imu_mutex);

        printk("LSM6DSL sensor samples: \r\n");
        printk("gyro z: %d deg/s | accel x: %f m/s^2\r\n\r\n", gyro_deg, accel_double);

        k_msleep(500);
    }
}

K_THREAD_DEFINE(sensor_readout, STACK_SIZE, sensor_readout_thread, NULL, NULL, NULL, PRIORITY, 0,
                10000);
