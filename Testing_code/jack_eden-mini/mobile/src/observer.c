/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "observer.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/sys/printk.h"
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/net_buf.h>
#include <zephyr/shell/shell.h>

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define NODE_NAME_LEN 7
const uint8_t mfr_match[] = {0x4C, 0x00, 0x02, 0x15};

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

// Struct for the bluetooth callback function user input
struct mfs_user_data {
    bool valid;
    uint8_t uuid[16]; // Might not need this...
    uint16_t major;
    uint16_t minor;
    int8_t cali_rssi;
};

struct k_work_delayable timeout_work;

/* ========================================================================== */
/* Scan Callbacks                                                             */
/* ========================================================================== */

static bool parse_mfr_cb(struct bt_data *data, void *user_data)
{
    struct mfs_user_data *user = (struct mfs_user_data *)user_data;

    if (data->type != BT_DATA_MANUFACTURER_DATA) {
        return true; // keep looking
    }

    // Matching 0x4C, 0x00, 0x02, 0x15
    for (int i = 0; i < 4; i++) {
        if (data->data[i] != mfr_match[i]) {
            user->valid = false;
            return false; // stop looking
        }
    }

    // Get UUID data
    int UUID_OFFSET = 4;
    for (int i = 0; i < 16; i++) {
        user->uuid[i] = data->data[i + UUID_OFFSET];
    }

    // Get Major
    int MAJOR_OFFSET = 20;
    user->major = 0;
    for (int i = 0; i < 2; i++) {
        user->major |= data->data[i + MAJOR_OFFSET] << ((1 - i) * 8);
    }

    // Get Minor
    int MINOR_OFFSET = 22;
    user->minor = 0;
    for (int i = 0; i < 2; i++) {
        user->minor |= data->data[i + MINOR_OFFSET] << ((1 - i) * 8);
    }

    // Done
    user->valid = true;
    return false;
}

static bool parse_name_cb(struct bt_data *data, void *user_data)
{
    char *name = user_data;
    uint8_t len;

    switch (data->type) {
    case BT_DATA_NAME_SHORTENED:
    case BT_DATA_NAME_COMPLETE:
        len = MIN(data->data_len, NODE_NAME_LEN - 1);
        (void)memcpy(name, data->data, len);
        name[len] = '\0';
        return false;
    default:
        return true;
    }
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
    struct net_buf_simple_state state;
    struct mfs_user_data manufacture = {.valid = false};

    // store Address & rssi of iBeacon.
    bt_addr_le_t address = *info->addr;
    int8_t rssi = info->rssi;

    // Merge the MFR data, rssi, Name and addr into one struct and send it.
    struct iBeacon node = {
        .rssi = rssi,      // From parse MFR
        .address = address // From info param
    };

    // Extract name
    (void)memset(node.name, 0, sizeof(node.name));
    net_buf_simple_save(buf, &state);             // save state
    bt_data_parse(buf, parse_name_cb, node.name); // consumes buf
    net_buf_simple_restore(buf, &state);          // restore

    bt_data_parse(buf, parse_mfr_cb, &manufacture);

    // This handles the filtering
    if (!manufacture.valid) {
        // Invalid packet
        return;
    }

    // Valid packet found
    node.major = manufacture.major;
    node.minor = manufacture.minor;

    /* ============================== */
    /* Do shit with the filtered data */
    /* ============================== */

    // Send via NUS
    int ret = bt_nus_send(NULL, (uint8_t *)&node, sizeof(node));
    if (ret == 0) {
        k_work_reschedule(&timeout_work, K_SECONDS(10));
    }
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv,
};

static void observer_register_callbacks(void)
{
    static bool isRegistered = false;

    if (!isRegistered) {
        int err = bt_le_scan_cb_register(&scan_callbacks);
        if (err < 0) {
            printk("[ERROR] Callback register failed (err: %d)", err);
            return;
        }

        isRegistered = true;
        printk("Registered scan callbacks\n");
    }
}

/* ========================================================================== */
/* Observer Control                                                           */
/* ========================================================================== */

int observer_start(void)
{
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    observer_register_callbacks();
    int err = bt_le_scan_start(&scan_param, NULL); // disable basic
    if (err) {
        printk("[ERROR] Start scanning failed (err %d)\n", err);
        return err;
    }

    printk("Started scanning...\n");
    return 0;
}

int observer_stop(void)
{
    int err = bt_le_scan_stop();
    if (err < 0) {
        printk("[ERROR] Stop scanning failed (err: %d)", err);
        return err;
    }

    printk("Scanning Stopped");
    return 0;
}
