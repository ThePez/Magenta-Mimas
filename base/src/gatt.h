/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_H
#define GATT_H

#include "zephyr/kernel.h"
#include <stdint.h>

struct helm_status_data {
    int64_t timestamp;
    uint16_t mv;
    uint8_t id;
};

struct helm_control_data {
    int64_t timestamp;
    int64_t halleffect_time;
    int16_t x;
    int16_t y;
    int16_t z;
    uint8_t id;
};

int start_scan(void);
int send_sync_pulse_to_helms(uint8_t *data, uint16_t len);
int close_connection(int slot);
int set_static_address(void);

/* Message Queue containing recieved data */
extern struct k_msgq helm_msg_queue;

#endif
