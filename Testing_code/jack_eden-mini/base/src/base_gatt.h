/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BASE_GATT_H
#define BASE_GATT_H

#include "zephyr/kernel.h"

int start_scan(void);
int close_connection(void);

/* Message Queue containing recieved data */
extern struct k_msgq ibeacon_msg_queue;

#endif
