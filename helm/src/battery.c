/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

#define STACK_SIZE 1024
#define PRIORITY   2

/* Battery reading GPIO pin - active low */
static const struct gpio_dt_spec bat_chg_en_pin = GPIO_DT_SPEC_GET(DT_ALIAS(bat_chg_en_pin), gpios);
static const struct device *const xiao_battery = DEVICE_DT_GET_ONE(xiao_battery);

static atomic_t charging_state = ATOMIC_INIT(false);

static int set_bat_charge(void)
{
    int ret = gpio_pin_set_dt(&bat_chg_en_pin, atomic_get(&charging_state));

    if (ret < 0) {
        printk("Error %d: Failed to set the battery reading state!\n", ret);
        return ret;
    }

    return 0;
}

int initialise_bat_charge(void)
{
    int ret;
    if (!gpio_is_ready_dt(&bat_chg_en_pin)) {
        printk("Error: bat_chg_en_pin device %s is not ready\n", bat_chg_en_pin.port->name);
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&bat_chg_en_pin, GPIO_OUTPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret, bat_chg_en_pin.port->name,
               bat_chg_en_pin.pin);
        return ret;
    }

    atomic_set(&charging_state, false);
    return set_bat_charge();
}

/* BELOW IS JUST FOR TESTING */
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static int cmd_bat_chg_en(const struct shell *sh, size_t argc, char **argv)
{
    atomic_set(&charging_state, true);
    return set_bat_charge();
}

static int cmd_bat_chg_dis(const struct shell *sh, size_t argc, char **argv)
{
    atomic_set(&charging_state, false);
    return set_bat_charge();
}

static int cmd_bat_chg_get(const struct shell *sh, size_t argc, char **argv)
{
    bool value = atomic_get(&charging_state);
    shell_print(sh, "Battery is currently%scharging", value ? " " : " not ");
    return 0;
}

static int cmd_voltage_read(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(xiao_battery)) {
        shell_error(sh, "Error - unable to start xiao battery\n");
        return -1;
    }

    struct sensor_value sensor_voltage;
    sensor_sample_fetch_chan(xiao_battery, SENSOR_CHAN_VOLTAGE);
    sensor_channel_get(xiao_battery, SENSOR_CHAN_VOLTAGE, &sensor_voltage);

    shell_print(sh, "Voltage reading: %.3fV", sensor_value_to_double(&sensor_voltage));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(battery_chg_cmds,
                               SHELL_CMD(enable, NULL, "Enable charging", cmd_bat_chg_en),
                               SHELL_CMD(disable, NULL, "Disable charging", cmd_bat_chg_dis),
                               SHELL_CMD(get, NULL, "Get charging state", cmd_bat_chg_get),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(battery_cmds,
                               SHELL_CMD(volt, NULL, "Read voltage", cmd_voltage_read),
                               SHELL_CMD(chg, &battery_chg_cmds, "Battery charging", NULL),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(battery, &battery_cmds, "Battery commands", NULL);
