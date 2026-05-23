/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MAC_H
#define MAC_H

#include <zephyr/bluetooth/bluetooth.h>

#define NUM_CONNECTIONS 2

const bt_addr_le_t base_addr = {
    .type = BT_ADDR_LE_RANDOM, .a.val = {0x88, 0xA3, 0xC1, 0xA0, 0xF2, 0xC3}
    // C3:F2:A0:C1:A3:88
};

/* Helm chip addresses - hardcoded for filtering */
const bt_addr_le_t helm_addr[NUM_CONNECTIONS] = {
    [0] =
        {
            .type = BT_ADDR_LE_RANDOM, .a.val = {0x56, 0x63, 0xCD, 0x44, 0x4A, 0xE1}
            // E1:4A:44:CD:63:56
        },
    [1] =
        {
            .type = BT_ADDR_LE_RANDOM, .a.val = {0xDF, 0x01, 0xF1, 0x02, 0x44, 0xE7}
            // E7:44:02:F1:01:DF
        },
};

#endif
