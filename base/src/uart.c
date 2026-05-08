/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart.h"

#ifdef UART_USB_C // Disables the file if not found

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
#define TX_BUF_SIZE        2048 // Enough to hold view -a

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

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

    while (1) {
        k_msleep(25);
    }
}

K_THREAD_DEFINE(uart_thread, 4096, uart_thread_entry, NULL, NULL, NULL, 6, 0, 0);

#endif