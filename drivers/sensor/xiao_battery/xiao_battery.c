#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#define DT_DRV_COMPAT xiao_battery

#define POST_GPIO_WAIT_MS 10
#define PRIORITY          (-1)
#define STACK_SIZE        1024

#define BATTERY_VOLTAGE_LEVELS 11
#define VOLTAGE_DIVIDER_XIAO   (510 / 1000)

LOG_MODULE_REGISTER(xiao_bat, CONFIG_SENSOR_LOG_LEVEL);
K_THREAD_STACK_DEFINE(sample_thread_stack, STACK_SIZE);

struct xiao_bat_data {
    struct k_thread sampling_thread;
    atomic_t raw;
    int32_t millivolts;
};

struct xiao_bat_config {
    const struct adc_dt_spec adc;
    const struct gpio_dt_spec read_pin;
    const int sampling_time;
};

static const int32_t battery_voltages[BATTERY_VOLTAGE_LEVELS] = {
    [0] = 3200, [1] = 3500, [2] = 3600, [3] = 3700, [4] = 3750,  [5] = 3800,
    [6] = 3850, [7] = 3900, [8] = 3950, [9] = 4000, [10] = 4200,
};

static void xiao_bat_sample_thread(void *dataVoid, void *cfgVoid, void *)
{
    struct xiao_bat_data *data = (struct xiao_bat_data *)dataVoid;
    const struct xiao_bat_config *cfg = (const struct xiao_bat_config *)cfgVoid;

    uint16_t buf;
    struct adc_sequence seq = {.buffer = &buf, .buffer_size = sizeof(buf)};

    int ret = adc_sequence_init_dt(&(cfg->adc), &seq);
    if (ret < 0) {
        LOG_ERR("Channel sequence failed: %d", ret);
        return;
    }

    while (1) {
        if (gpio_pin_set_dt(&(cfg->read_pin), 0) < 0) {
            LOG_ERR("Failed to set battery read pin low");
        }

        k_msleep(POST_GPIO_WAIT_MS);
        ret = adc_read_dt(&(cfg->adc), &seq);

        if (ret < 0) {
            LOG_ERR("Error reading ADC: %d", ret);
        } else {
            atomic_set(&(data->raw), buf);
        }

        if (gpio_pin_set_dt(&(cfg->read_pin), 1) < 0) {
            LOG_ERR("Failed to set battery read pin high");
        }

        k_sleep(K_SECONDS(cfg->sampling_time));
    }
}

static int xiao_bat_init(const struct device *dev)
{
    struct xiao_bat_data *data = dev->data;
    const struct xiao_bat_config *cfg = dev->config;

    if (!adc_is_ready_dt(&(cfg->adc))) {
        LOG_ERR("Device %s is not ready", cfg->adc.dev->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&(cfg->read_pin))) {
        LOG_ERR("Device %s is not ready", cfg->read_pin.dev->name);
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&(cfg->read_pin), GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("GPIO not configured as output: %d", ret);
        return ret;
    }

    ret = adc_channel_setup_dt(&(cfg->adc));
    if (ret < 0) {
        LOG_ERR("Channel setup failed: %d", ret);
        return ret;
    }

    k_thread_create(&(data->sampling_thread), sample_thread_stack,
                    K_THREAD_STACK_SIZEOF(sample_thread_stack),
                    xiao_bat_sample_thread, data, (void *)cfg, NULL, PRIORITY,
                    0, K_NO_WAIT);

    return 0;
}

static int xiao_bat_sample_fetch(const struct device *dev,
                                 enum sensor_channel chan)
{
    struct xiao_bat_data *data = dev->data;

    if (chan == SENSOR_CHAN_VOLTAGE ||
        chan == SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) {

        int32_t dataBuffer = (int32_t)atomic_get(&(data->raw));
        int ret = adc_raw_to_millivolts_dt(&(cfg->adc), &dataBuffer);

        if (ret < 0) {
            LOG_ERR("Unable to convert to mv: %d", ret);
            return ret;
        }

        data->millivolts = dataBuffer / VOLTAGE_DIVIDER_XIAO;
        return 0;
    }

    return -ENOTSUP;
}

static int xiao_bat_channel_get(const struct device *dev,
                                enum sensor_channel chan,
                                struct sensor_value *val)
{
    struct xiao_bat_data *data = dev->data;
    const struct xiao_bat_config *cfg = dev->config;

    int32_t millivolts = data->millivolts;

    if (chan == SENSOR_CHAN_VOLTAGE) {
        return sensor_value_from_milli(val, millivolts);
    } else if (chan == SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) {
        int current_index = 0;
        while (current_index < BATTERY_VOLTAGE_LEVELS &&
                millivolts < battery_voltages[current_index++]) {}
        return 10 * current_index;
    }

    return -ENOTSUP;
}

static DEVICE_API(sensor, xiao_bat_driver_api) = {
    .sample_fetch = xiao_bat_sample_fetch, .channel_get = xiao_bat_channel_get};

#define XIAO_BATTERY_INIT(index)                                               \
    static struct xiao_bat_data xiao_bat_data_##index = {                      \
        .raw = ATOMIC_INIT(0),                                                 \
    };                                                                         \
    static struct xiao_bat_config xiao_bat_config_##index = {                  \
        .adc = ADC_DT_SPEC_INST_GET(index),                                    \
        .read_pin = GPIO_DT_SPEC_INST_GET(index, read_gpios),                  \
        .sampling_time = DT_INST_PROP(index, sampling_time)};                  \
    SENSOR_DEVICE_DT_INST_DEFINE(                                              \
        index, &xiao_bat_init, NULL, &xiao_bat_data_##index,                   \
        &xiao_bat_config_##index, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,    \
        &xiao_bat_driver_api);

DT_INST_FOREACH_STATUS_OKAY(XIAO_BATTERY_INIT)
