/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_H
#define GATT_H

#include "zephyr/kernel.h"

int start_scan(void);
int close_connection(int slot);
int set_static_address(void);

/* Message Queue containing recieved data */
extern struct k_msgq ibeacon_msg_queue;

#endif
