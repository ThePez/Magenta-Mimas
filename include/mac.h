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
    .type = BT_ADDR_LE_RANDOM, .a.val = {0xBB, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
    // FF:EE:DD:CC:BB:BB  <-- replace with Base address
};

/* Helm chip addresses - hardcoded for filtering */
const bt_addr_le_t helm_addr[NUM_CONNECTIONS] = {
    [0] =
        {
            .type = BT_ADDR_LE_RANDOM, .a.val = {0x56, 0x63, 0xCD, 0x44, 0x4A, 0xE1}
            // E1:4A:44:CD:63:56 <-- replace with Helm_A address
        },
    [1] =
        {
            .type = BT_ADDR_LE_RANDOM, .a.val = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
            // FF:EE:DD:CC:BB:AA  <-- replace with Helm_B address
        },
};

#endif
