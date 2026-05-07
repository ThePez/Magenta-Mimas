/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rb_tree.h"
#include "uart.h"
#include <stdint.h>

#ifdef UART_USB_C // Disables the file if not found

#include "base_gatt.h"
#include "observer.h"
#include "json.h"
#include "localisation.h"
#include "ble_shell.h"
#include "kalman.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "zephyr/bluetooth/addr.h"
#include "zephyr/toolchain.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <stddef.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define UART_JSON_BUF_SIZE 256
#define TX_BUF_SIZE        2048 // Enough to hole view -a

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

// Buffers for json strings

static char x_str[16];
static char y_str[16];
static char time_str[24];
static char addr_str[BT_ADDR_LE_STR_LEN];
static char rssi_val_strs[NUM_BEACONS][5];
static char major_str[7];
static char minor_str[7];
static char rssi_str[5];

// Buffers for complete json packets

static char rx_json_buffer[UART_JSON_BUF_SIZE];
static char tx_json_buffer[UART_JSON_BUF_SIZE * 2]; // pos rssi is close to 256

// Flag for the UART interrupt driver
static bool initialised = false;

// Buffer for each line input
static char rx_buf[UART_JSON_BUF_SIZE];
static int rx_buf_pos;

// RX: message queue for complete lines
K_MSGQ_DEFINE(uart_rx_msgq, sizeof(rx_buf), 4, 4);

// TX: ring buffer fed by threads, drained by ISR
RING_BUF_DECLARE(tx_ring_buf, TX_BUF_SIZE);

// This uses a different UART -> connected to the USB-C
static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

/* ========================================================================== */
/* UART Send                                                                  */
/* Pushes bytes into the TX ring buffer and enables the TX IRQ.               */
/* The ISR drains the ring buffer byte by byte into the UART FIFO.            */
/* ========================================================================== */

/* Byte length uart send function */
int uart_send(const char *buf, size_t len)
{
    // Push requested char's into the TX ring buffer
    uint32_t written = ring_buf_put(&tx_ring_buf, (const uint8_t *)buf, len);
    if (written < len) {
        // Buffer is full
        written = (-ENOSPC);
    }

    // TX now has data to send -> enable the IRQ.
    uart_irq_tx_enable(uart_dev);
    return (written);
}

/* Wrapper for null terminated C strings */
int print_uart(const char *str)
{
    return uart_send(str, strlen(str));
}

/* ========================================================================== */
/* ISR                                                                        */
/* Handles TX and RX interrupts.                                              */
/* TX: drains the ring buffer into the UART FIFO one byte at a time.          */
/* RX: accumulates chars into rx_buf, enqueues complete lines on newline.     */
/* ========================================================================== */

static void serial_cb(const struct device *dev, void *user_data)
{
    // This function needs to be called first
    if (!uart_irq_update(dev)) {
        return;
    }

    // TX: Is TX ready?
    if (uart_irq_tx_ready(dev)) {
        uint8_t c;
        // Drain the tx ring buffer into the TX FIFO char by char
        while (ring_buf_peek(&tx_ring_buf, &c, 1) == 1) {
            // If a byte available pass to the uart fifo
            if (uart_fifo_fill(dev, &c, 1) == 0) {
                // FIFO is full -> stop for now
                break;
            }

            // byte was sent -> discard it from the ring buffer now
            ring_buf_get(&tx_ring_buf, NULL, 1);
        }

        // No bytes waiting for send -> disable TX IRQ
        if (ring_buf_is_empty(&tx_ring_buf)) {
            uart_irq_tx_disable(dev);
        }
    }

    // RX: Is RX ready?
    if (uart_irq_rx_ready(dev)) {
        uint8_t c;
        // Drain the RX fifo char by char
        while (uart_fifo_read(dev, &c, 1) == 1) {
            if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
                // If a newline is returned terminate the string and pass to queue
                rx_buf[rx_buf_pos] = '\0';
                k_msgq_put(&uart_rx_msgq, &rx_buf, K_NO_WAIT);
                rx_buf_pos = 0;
            } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
                // Otherwise if space available add to rx word buffer
                rx_buf[rx_buf_pos++] = c;
            }
        }
    }
}

/* ========================================================================== */
/* UART Initialisation                                                        */
/* Registers the ISR callback and enables the RX interrupt.                   */
/* TX interrupt is only enabled on demand when data is queued.                */
/* ========================================================================== */

/* Initialises the IQR Uart */
static int uart_interrupt_driver_init(void *user_data)
{
    if (initialised) {
        return (-EALREADY);
    }

    // Check if device ready
    if (!device_is_ready(uart_dev)) {
        printk("[ERROR] UART device not ready\n");
        return (-EBUSY);
    }

    // Could use @param user_data to pass a struct of some kind to the ISR
    int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, user_data);
    if (ret < 0) {
        // Errors
        if (ret == -ENOTSUP) {
            printk("[ERROR] Interrupt-driven UART API not enabled\n");
        } else if (ret == -ENOSYS) {
            printk("[ERROR] UART device does not support interrupt-driven API\n");
        } else {
            printk("[ERROR] Error setting UART callback: %d\n", -ret);
        }

        return (ret);
    }

    // Enable the RX ISR
    uart_irq_rx_enable(uart_dev);
    // Only enable tx ISR when there is data to send

    // RX/TX Interrupt driven UART setup
    printk("[INFO] UART initialised\n");
    initialised = true;
    return (0);
}

static void send_position_json(char *json_buffer, size_t buf_size, struct cmd_json *pos_cmd,
                               char *loc)
{
    memset(json_buffer, 0, buf_size);
    int ret = encode_json_cmd(pos_cmd, json_buffer, buf_size);
    if (ret == 0) {
        print_uart(json_buffer);
        print_uart("\n");
    } else {
        printk("[ERROR] %s, code: %d, Position JSON packet encode failed\n", loc, ret);
    }
}

static void build_position_strings(double x, double y, int64_t timestamp)
{
    snprintf(x_str, sizeof(x_str), "%f", x);
    snprintf(y_str, sizeof(y_str), "%f", y);
    snprintf(time_str, sizeof(time_str), "%lld", timestamp);
}

/* ========================================================================== */
/* UART Thread                                                                */
/* Decodes incoming JSON command strings from the GUI and dispatches them     */
/* via shell_dispatch(). In STANDARD mode, encodes position updates from      */
/* localisation and sends to the GUI. In LISTEN mode, encodes raw iBeacon     */
/* packets and sends them instead.                                            */
/* ========================================================================== */

static void uart_thread_entry(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uart_interrupt_driver_init(NULL);

    struct all_position sensorPosition = {0};
    struct iBeacon bleBeacon = {0};
    struct cmd_json positionJson = {.cmd = "pos", .opts = {x_str, y_str, time_str}};

    /* clang-format off */
    struct cmd_json rssiJson = {
        .cmd = "pos", .sub = "rssi", .len = 26, 
        .opts = {
            [0] = "4011-A",[2] = "4011-B",[4] = "4011-C",[6] = "4011-D",
            [8] = "4011-E", [10] = "4011-F", [12] = "4011-G", [14] = "4011-H",
            [16] = "4011-I", [18] = "4011-J", [20] = "4011-K", [22] = "4011-L",
            [24] = "4011-M"},
    };
    /* clang-format on */

    while (1) {
        // Sleep at the start of each loop (less nesting)
        k_msleep(25);

        /* ------------------------------------------------------------------ */
        /* RX: Decode incoming JSON command from GUI                          */
        /* ------------------------------------------------------------------ */

        if (k_msgq_get(&uart_rx_msgq, &rx_json_buffer, K_NO_WAIT) == 0) {
            struct cmd_json shell_cmd = {0};
            if (decode_json_cmd(rx_json_buffer, &shell_cmd) == 0) {
                // Valid JSON decode

                // Build argv and argc
                char *argv[MAX_CMD_OPTS + 1];
                argv[0] = shell_cmd.sub;
                for (int i = 0; i < shell_cmd.len; i++) {
                    argv[i + 1] = shell_cmd.opts[i];
                }

                size_t argc = 1 + shell_cmd.len;
                shell_dispatch(argc, argv, &shell_cmd);
                // Clear buffer once done
                memset(rx_json_buffer, 0, sizeof(rx_json_buffer));
            }
        }

        /* ------------------------------------------------------------------ */
        /* TX: Encode and send position update to GUI (empty in listen mode)  */
        /* ------------------------------------------------------------------ */

        if (k_msgq_get(&kalman_msg_queue, &sensorPosition, K_NO_WAIT) == 0) {
            // Send {"cmd":"pos", "sub":"start","opts":[]}
            size_t buf_len = sizeof(tx_json_buffer);
            positionJson.sub = "start";
            positionJson.len = 0;
            send_position_json(tx_json_buffer, buf_len, &positionJson, "START");

            // sub = raw for unfiltered
            positionJson.sub = "raw";
            positionJson.len = 3;
            build_position_strings(sensorPosition.raw.x, sensorPosition.raw.y,
                                   sensorPosition.raw.timestamp);
            send_position_json(tx_json_buffer, buf_len, &positionJson, "RAW");

            // sub = kalman for kalman filtered data
            positionJson.sub = "kalman";
            build_position_strings(sensorPosition.kalman.x, sensorPosition.kalman.y,
                                   sensorPosition.kalman.timestamp);
            send_position_json(tx_json_buffer, buf_len, &positionJson, "KALMAN");

            // Now send RSSI's for this measurement
            // send {"cmd":"pos","sub":"rssi","opts":["name",value,........"]}

            // Pre-fill all odd slots with "0"
            for (int i = 0; i < NUM_BEACONS; i++) {
                snprintf(rssi_val_strs[i], sizeof(rssi_val_strs[i]), "0");
                rssiJson.opts[i * 2 + 1] = rssi_val_strs[i];
            }

            for (int i = 0; i < sensorPosition.raw.len; i++) {
                // Get node for the name from the ID
                struct beacon_data *node = get_rb_node(sensorPosition.raw.ids[i]);
                if (node == NULL) {
                    continue;
                }

                // node = data_list[x] => data_list + x
                // thus x = node - data_list
                int j = node - data_list; // Pointer arithmtic, saves a loop :)
                snprintf(rssi_val_strs[j], sizeof(rssi_val_strs[j]), "%d",
                         sensorPosition.raw.readings[i]);
            }

            // Send the rssi's
            send_position_json(tx_json_buffer, buf_len, &rssiJson, "RSSI");

            // Send {"cmd":"pos", "sub":"stop","opts":[]}
            positionJson.sub = "stop";
            positionJson.len = 0;
            send_position_json(tx_json_buffer, buf_len, &positionJson, "STOP");
        }

        // Skip rest of loop if in Standard mode
        if (atomic_get(&mode) == STANDARD_MODE) {
            continue;
        }

        /* ------------------------------------------------------------------ */
        /* TX: Encode and send iBeacon packet to GUI (LISTEN_MODE required)   */
        /* ------------------------------------------------------------------ */
        if (k_msgq_get(&ibeacon_msg_queue, &bleBeacon, K_NO_WAIT) == 0) {
            // Send iBeacon packets to the GUI
            bt_addr_le_to_str(&bleBeacon.address, addr_str, sizeof(addr_str));
            snprintf(major_str, sizeof(major_str), "0x%04X", bleBeacon.major);
            snprintf(minor_str, sizeof(minor_str), "0x%04X", bleBeacon.minor);
            snprintf(rssi_str, sizeof(rssi_str), "%d", bleBeacon.rssi);

            struct beacon_json pkt = {
                .cmd = "found",         // Type of JSON expected by GUI
                .name = bleBeacon.name, // Beacon Name
                .addr = addr_str,       // BLE addr
                .major = major_str,     // Beacon major
                .minor = minor_str,     // Beacon minor
                .rssi = rssi_str        // beacon calibrated rssi
            };

            // Clear buffer before use
            memset(tx_json_buffer, 0, sizeof(tx_json_buffer));
            if (encode_json_beacon(&pkt, tx_json_buffer, sizeof(tx_json_buffer)) == 0) {
                print_uart(tx_json_buffer);
                print_uart("\n");
            } else {
                print_uart("[ERROR] iBeacon JSON packet encode failed\n");
            }
        }
    }
}

K_THREAD_DEFINE(uart_thread, 4096, uart_thread_entry, NULL, NULL, NULL, 6, 0, 0);

#endif // UART_USB_C
