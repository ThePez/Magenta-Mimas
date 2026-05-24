/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/* ========================================================================== */
/* Devices                                                                    */
/* ========================================================================== */

static const struct device *const xiao_battery = DEVICE_DT_GET_ONE(xiao_battery);

/* ========================================================================== */
/* Initialisation and settings                                                */
/* ========================================================================== */

int get_battery_voltage(double *value)
{
    if (!device_is_ready(xiao_battery)) {
        printk("[ERROR] Unable to get xiao battery\n");
        return (-1);
    }

    struct sensor_value sensor_voltage;
    sensor_sample_fetch_chan(xiao_battery, SENSOR_CHAN_VOLTAGE);
    sensor_channel_get(xiao_battery, SENSOR_CHAN_VOLTAGE, &sensor_voltage);

    *value = sensor_value_to_double(&sensor_voltage);
    return (0);
}

int get_battery_charge(int32_t *value)
{
    if (!device_is_ready(xiao_battery)) {
        printk("[ERROR] Unable to get xiao battery\n");
        return (-1);
    }

    struct sensor_value sensor_charge;
    sensor_sample_fetch_chan(xiao_battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
    sensor_channel_get(xiao_battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &sensor_charge);

    *value = sensor_charge.val1;
    return (0);
}
