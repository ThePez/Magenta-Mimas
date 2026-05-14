/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "magnet.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Message queue of depth 1 - we are only interested in the last item. */
K_MSGQ_DEFINE(magnet_time_q, sizeof(int64_t), 1, 1);

/* Magnet pin interrupt - for hall effect or reed switch */
static const struct gpio_dt_spec magnet_pin = GPIO_DT_SPEC_GET(DT_ALIAS(magnet_pin), gpios);

static struct gpio_callback magnet_cb_data;

/* Callback function on pin interrupt */
static void magnet_pin_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    int64_t current_time_ms = k_uptime_get();

    // Try to add the currnet time - if the queue is full, purge it and try again
    while (k_msgq_put(&magnet_time_q, &current_time_ms, K_NO_WAIT) != 0) {
        k_msgq_purge(&magnet_time_q);
    }
}

int initialise_magnet_sensor(void)
{
    int ret;

    if (!gpio_is_ready_dt(&magnet_pin)) {
        printk("Error: magnet_pin device %s is not ready\n", magnet_pin.port->name);
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&magnet_pin, GPIO_INPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret, magnet_pin.port->name,
               magnet_pin.pin);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&magnet_pin, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n", ret, magnet_pin.port->name,
               magnet_pin.pin);
        return ret;
    }

    gpio_init_callback(&magnet_cb_data, magnet_pin_cb, BIT(magnet_pin.pin));
    ret = gpio_add_callback(magnet_pin.port, &magnet_cb_data);
    if (ret != 0) {
        printk("Error %d: failed to configure callback on %s pin %d\n", ret, magnet_pin.port->name,
               magnet_pin.pin);
        return ret;
    }

    return 0;
}
