#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

#define BLINK_VAL(val) ((val >> 3) & 0x01)
#define RED_VAL(val)   ((val >> 2) & 0x01)
#define GREEN_VAL(val) ((val >> 1) & 0x01)
#define BLUE_VAL(val)  ((val >> 0) & 0x01)

#define STACKSIZE 1024
#define PRIORITY  7

typedef struct {
    uint8_t device;
    uint8_t length;
    uint8_t data[4]; // way over kill....
} uart_message_t;

/////////////////////////// This is the PIN using the interrupt ////////////////
// Uart coms begin pin (both PSU & AHU)
static const struct gpio_dt_spec uart_pin = GPIO_DT_SPEC_GET(DT_ALIAS(compin), gpios);

// Uart decive (either PSU or AHU)
static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

// Using the aliases from: zephyr/boards/seeed/xiao_ble/xiao_ble_common.dtsi
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/////////////////// NEED THIS FOR THE CALLBACK ////////////////////
static struct gpio_callback uart_cb_data;

// Queues
K_MSGQ_DEFINE(led_queue_psu, sizeof(uint8_t), 10, 4);
K_MSGQ_DEFINE(rng_queue_psu, sizeof(uint16_t), 10, 4);
// Signals
K_SEM_DEFINE(uart_sem_psu, 0, 1);
K_SEM_DEFINE(rng_sem_psu, 0, 1);

/* Sets up the GPIO spec given to be an output */
int configure_gpio(const struct gpio_dt_spec *pin)
{
    if (!gpio_is_ready_dt(pin)) {
        return 1;
    }

    if (gpio_pin_configure_dt(pin, GPIO_OUTPUT_ACTIVE) < 0) {
        return 1;
    }

    return 0;
}

/* CALLBACK FUNCTION FOR A PIN INTERRUPT */
void uart_start_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // Signal the UART PSU Thread
    k_sem_give(&uart_sem_psu);
}

bool parse_cmd(uart_message_t *cmd)
{
    uint8_t byte;
    if (uart_poll_in(uart_dev, &byte) != 0 || byte != 0x01) {
        // Either no data available or first byte was invalid (ie not 0x01)
        return false;
    }

    // Get length
    uint8_t length;
    if (uart_poll_in(uart_dev, &length) != 0 || length < 1 || length > sizeof(cmd->data)) {
        return false;
    }

    // get data payload
    cmd->length = length;

    uint8_t device = 0;
    if (uart_poll_in(uart_dev, &device) != 0 || device == 0) {
        return false;
    }

    // Assign device
    cmd->device = device;

    for (uint8_t i = 0; i < cmd->length; i++) {
        if (uart_poll_in(uart_dev, &cmd->data[i]) != 0) {
            return false;
        }
    }

    return true;
}

void thread_uart_PSU_entry(void *dummy1, void *dummy2, void *dummy3)
{
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);

    uint16_t rnd;

    while (1) {
        uart_message_t cmd = {0};
        k_sem_take(&uart_sem_psu, K_FOREVER);
        if (parse_cmd(&cmd)) {
            if (cmd.device == 0x01) {
                // LED -> no responses
                k_msgq_put(&led_queue_psu, &cmd.data[0], K_NO_WAIT);
            } else if (cmd.device == 0x02) {
                // RNG -> gives a response
                k_sem_give(&rng_sem_psu);
                k_msgq_get(&rng_queue_psu, &rnd, K_SECONDS(1));
                uart_poll_out(uart_dev, 0x02);              // response
                uart_poll_out(uart_dev, 0x02);              // length
                uart_poll_out(uart_dev, 0x02);              // RNG device
                uart_poll_out(uart_dev, rnd & 0xFF);        // Lower byte
                uart_poll_out(uart_dev, (rnd >> 8) & 0xFF); // Upper byte
            }
        }

        k_msleep(100);
    }
}

void thread_gen_entry_point(void *dummy1, void *dummy2, void *dummy3)
{
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);

    uint16_t rnd;

    while (1) {
        k_sem_take(&rng_sem_psu, K_FOREVER);
        // Get random number
        rnd = sys_rand16_get();
        // Put random number on queue
        k_msgq_put(&rng_queue_psu, &rnd, K_NO_WAIT);
    }
}

void thread_led_entry(void *dummy1, void *dummy2, void *dummy3)
{
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);

    // Configure the GPIO pins for each led
    if (configure_gpio(&led_red)) {
        return;
    }

    if (configure_gpio(&led_green)) {
        return;
    }

    if (configure_gpio(&led_blue)) {
        return;
    }

    bool state = false;
    uint8_t col, red, green, blue, blink;
    col = 0;
    blink = 0;

    while (1) {
        // Get the colour from the queue
        k_msgq_get(&led_queue_psu, &col, K_SECONDS(1));

        // Extract the blink & R/G/B from the bitmask
        blink = BLINK_VAL(col);
        red = RED_VAL(col);
        green = GREEN_VAL(col);
        blue = BLUE_VAL(col);

        // If blink is set alternate ON/OFF
        if (blink && state) {
            gpio_pin_set_dt(&led_red, 0);
            gpio_pin_set_dt(&led_green, 0);
            gpio_pin_set_dt(&led_blue, 0);
            state = false;
        } else {
            // Otherwise just set the colour every second
            gpio_pin_set_dt(&led_red, red);
            gpio_pin_set_dt(&led_green, green);
            gpio_pin_set_dt(&led_blue, blue);
            if (blink) {
                state = true;
            }
        }
    }
}

/////////////////////////////// PSU CHIP THREADS ///////////////////////////////////

// Spawn the RNG thread if on the PSU chip
K_THREAD_DEFINE(thread_rng, STACKSIZE, thread_gen_entry_point, NULL, NULL, NULL, PRIORITY, 0, 0);
extern const k_tid_t thread_rng;

// Spawn the LED thread if on the PSU chip
K_THREAD_DEFINE(thread_led, STACKSIZE, thread_led_entry, NULL, NULL, NULL, PRIORITY, 0, 0);
extern const k_tid_t thread_led;

// Spawn the PSU UART thread
K_THREAD_DEFINE(thread_uart_psu, STACKSIZE, thread_uart_PSU_entry, NULL, NULL, NULL, PRIORITY, 0,
                0);

extern const k_tid_t thread_uart_psu;

/* Sets up the UART pin D0 as an input and configures the callback function */
int main(void)
{
    int ret;
    if (!gpio_is_ready_dt(&uart_pin)) {
        printk("Error: uart_pin device %s is not ready\n", uart_pin.port->name);
        return 0;
    }

    ret = gpio_pin_configure_dt(&uart_pin, GPIO_INPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n", ret, uart_pin.port->name, uart_pin.pin);
        return 0;
    }

    ret = gpio_pin_interrupt_configure_dt(&uart_pin, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n", ret, uart_pin.port->name,
               uart_pin.pin);
        return 0;
    }

    gpio_init_callback(&uart_cb_data, uart_start_cb, BIT(uart_pin.pin));
    gpio_add_callback(uart_pin.port, &uart_cb_data);
    printk("Set up uart_pin at %s pin %d\n", uart_pin.port->name, uart_pin.pin);
    return 0;
}
