/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_H
#define GATT_H

#include "zephyr/kernel.h"

int initialise_base_gatt(void);
int send_sync_pulse_to_helms(uint8_t *data, uint16_t len);

/* Message Queue containing recieved data */
extern struct k_msgq gatt_msg_queue;

#endif
