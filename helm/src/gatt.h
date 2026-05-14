/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_H
#define GATT_H

// #define WHITELIST // Enables connection whitelisting
#define CHIP_A    // Swaps the name

#ifdef CHIP_A
#define DEVICE_NAME "Helm-A"
#else
#define DEVICE_NAME "Helm-B"
#endif

#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#include <zephyr/kernel.h>

extern struct k_sem notif_sem;

int initialise_helm_gatt(void);
int send_data_nus(const void* data, uint16_t len);

#endif
