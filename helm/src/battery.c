/* Magnetic sensor - reed/HE TBD */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define STACK_SIZE 1024
#define PRIORITY   2

/* Battery reading GPIO pin - active low */
static const struct gpio_dt_spec bat_chg_en_pin =
    GPIO_DT_SPEC_GET(DT_ALIAS(bat_chg_en_pin), gpios);
static const struct device *const xiao_bat = DEVICE_DT_GET_ONE(xiao_battery);

int set_bat_charge(bool state)
{
    int ret = gpio_pin_set_dt(&bat_chg_en_pin, !state);

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
        printk("Error: bat_chg_en_pin device %s is not ready\n",
               bat_chg_en_pin.port->name);
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&bat_chg_en_pin, GPIO_OUTPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret,
               bat_chg_en_pin.port->name, bat_chg_en_pin.pin);
        return ret;
    }

    return set_bat_chg_en(false);
}

/* BELOW IS JUST FOR TESTING */
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static int cmd_voltage_read(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(xiao_bat)) {
        printk("Error - unable to start xiao battery\n");
        return -1;
    }

    struct sensor_value sensorVoltage;
    sensor_sample_fetch_chan(xiao_battery, SENSOR_CHAN_VOLTAGE);
    sensor_channel_get(xiao_battery, SENSOR_CHAN_VOLTAGE, &sensorVoltage);

    shell_print(sh, "Voltage reading: %.3fV",
                sensor_value_to_double(&sensorVoltage));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(battery_cmds,
                               SHELL_CMD(volt, NULL, "Read voltage",
                                         cmd_voltage_read),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(battery, &battery_cmds, "Battery commands", NULL);
