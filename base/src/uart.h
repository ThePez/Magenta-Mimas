/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UART_H
#define UART_H

// Use this define to make the UART use the USB-C UART
// #define UART_USB_C 1

#ifdef UART_USB_C
#include <stdio.h>

int uart_send(const char *buf, size_t len);
int print_uart(const char *str);

#endif // UART_USB_C end
#endif // UART_H end
