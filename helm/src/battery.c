/* Magnetic sensor - reed/HE TBD */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Battery reading GPIO pin - active low */
static const struct gpio_dt_spec bat_read_pin =
    GPIO_DT_SPEC_GET(DT_ALIAS(bat_read_en_pin), gpios);

int set_bat_read(int state)
{
    int ret = gpio_pin_set_dt(&bat_read_pin, state);

    if (ret < 0) {
        printk("Error %d: Failed to set the battery reading state!\n", ret);
        return ret;
    }

    return 0;
}

int initialise_bat_read(void)
{
    int ret;
    if (!gpio_is_ready_dt(&bat_read_pin)) {
        printk("Error: bat_read_pin device %s is not ready\n",
               bat_read_pin.port->name);
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&bat_read_pin, GPIO_OUTPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret,
               bat_read_pin.port->name, bat_read_pin.pin);
        return ret;
    }

    return set_bat_read(false);
}
