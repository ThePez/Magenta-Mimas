/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery.h"
#include "gatt.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

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
        printk("Error - unable to get xiao battery\n");
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
        printk("Error - unable to get xiao battery\n");
        return (-1);
    }

    struct sensor_value sensor_charge;
    sensor_sample_fetch_chan(xiao_battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
    sensor_channel_get(xiao_battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &sensor_charge);

    *value = sensor_charge.val1;
    return (0);
}

/* ========================================================================== */
/* Shell commands                                                             */
/* ========================================================================== */

static int cmd_bat_voltage_read(const struct shell *sh, size_t argc, char **argv)
{
    double voltage = 0;

    if (get_battery_voltage(&voltage) < 0) {
        return (-1);
    }

    shell_print(sh, "Voltage reading: %.3fV", voltage);
    return (0);
}

static int cmd_bat_charge_read(const struct shell *sh, size_t argc, char **argv)
{
    int32_t charge = 0;

    if (get_battery_charge(&charge) < 0) {
        return (-1);
    }

    shell_print(sh, "Charge reading: %d%%", charge);
    return (0);
}

SHELL_STATIC_SUBCMD_SET_CREATE(battery_cmds,
                               SHELL_CMD(voltage, NULL, "Read voltage", cmd_bat_voltage_read),
                               SHELL_CMD(charge, NULL, "Read charge", cmd_bat_charge_read),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(battery, &battery_cmds, "Battery commands", NULL);
