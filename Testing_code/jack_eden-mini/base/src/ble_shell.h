/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_SHELL_H
#define BLE_SHELL_H
#include "uart.h" // <- UART_USB_C
#include "json.h"
#include "zephyr/sys/atomic.h"

enum mode_t {
    STANDARD_MODE,
    LISTEN_MODE
};

#ifdef UART_USB_C
int shell_dispatch(size_t argc, char **argv, struct cmd_json *cmd);
#endif

// 1 for base is listen mode, 0 for normal mode
extern atomic_t mode;

#endif
