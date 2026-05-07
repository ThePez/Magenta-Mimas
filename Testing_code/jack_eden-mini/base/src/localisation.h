/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOCALISATION_H
#define LOCALISATION_H

#include "rb_tree.h"
#include <stdint.h>

struct position_data {
    int64_t timestamp;
    double x;
    double y;
    uint32_t ids[NUM_BEACONS];
    int8_t readings[NUM_BEACONS];
    uint8_t len;
};

/* Message Queue containing positional data */
extern struct k_msgq localisation_msg_queue;

#endif
