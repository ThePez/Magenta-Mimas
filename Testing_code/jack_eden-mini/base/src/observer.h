/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OBSERVER_H
#define OBSERVER_H

#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

#define NODE_NAME_LEN 7

struct iBeacon {
    char name[NODE_NAME_LEN];
    bt_addr_le_t address;
    uint16_t major;
    uint16_t minor;
    int8_t rssi;
};

int observer_start(void);
int observer_stop(void);

#endif
