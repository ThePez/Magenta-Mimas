/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "observer.h"
#include "base_gatt.h"

#include <stdint.h>
#include <string.h>

#include "zephyr/sys/printk.h"
#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/net_buf.h>

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define MATCH_SIZE    4
#define UUID_OFFSET   4
#define MAJOR_OFFSET  20
#define MINOR_OFFSET  22
#define UUID_SIZE     16
#define NODE_NAME_LEN 7

/* iBeacon manufacturer data prefix: Apple company ID + iBeacon type + length */
const uint8_t mfr_match[] = {0x4C, 0x00, 0x02, 0x15};

static bool isRegistered = false;

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

/* Parsed fields extracted from a manufacturer data AD element */
struct mfs_user_data {
    bool valid;
    uint8_t uuid[16];
    uint16_t major;
    uint16_t minor;
};

/* ========================================================================== */
/* AD Parsers                                                                 */
/* Called by bt_data_parse() for each AD element in an advertising packet.    */
/* Return true to keep iterating, false to stop.                              */
/* ========================================================================== */

/* Parses manufacturer data and extracts UUID, major, and minor if the       */
/* packet matches the iBeacon prefix.                                        */
static bool parse_mfr_cb(struct bt_data *data, void *user_data)
{
    struct mfs_user_data *user = (struct mfs_user_data *)user_data;

    if (data->type != BT_DATA_MANUFACTURER_DATA) {
        return true; // keep looking
    }

    // Match Apple iBeacon prefix
    for (int i = 0; i < MATCH_SIZE; i++) {
        if (data->data[i] != mfr_match[i]) {
            user->valid = false;
            return (false);
        }
    }

    // Extract UUID
    for (int i = 0; i < UUID_SIZE; i++) {
        user->uuid[i] = data->data[i + UUID_OFFSET];
    }

    // Extract Major
    user->major = 0;
    for (int i = 0; i < 2; i++) {
        user->major |= data->data[i + MAJOR_OFFSET] << ((1 - i) * 8);
    }

    // Extract Minor
    user->minor = 0;
    for (int i = 0; i < 2; i++) {
        user->minor |= data->data[i + MINOR_OFFSET] << ((1 - i) * 8);
    }

    user->valid = true;
    return (false);
}

/* Parses the device name (shortened or complete) from the AD elements. */
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
        return (false);
    default:
        return (true);
    }
}

/* ========================================================================== */
/* Scan Receive Callback                                                      */
/* Fires for every advertising packet seen during an active scan.             */
/* Parses manufacturer data and name, then forwards valid iBeacon packets     */
/* to the ibeacon_msg_queue for consumption by the localisation thread.       */
/* ========================================================================== */

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
    struct net_buf_simple_state state;
    struct mfs_user_data manufacture = {.valid = false};

    net_buf_simple_save(buf, &state);
    bt_data_parse(buf, parse_mfr_cb, &manufacture);
    net_buf_simple_restore(buf, &state);

    // Ignore non-iBeacon packets
    if (!manufacture.valid) {
        return;
    }

    struct iBeacon node = {
        .major = manufacture.major,
        .minor = manufacture.minor,
        .rssi = info->rssi,
        .address = *info->addr,
    };

    // Extract name
    (void)memset(node.name, 0, sizeof(node.name));
    bt_data_parse(buf, parse_name_cb, node.name);

    // Forward to iBeacon Queue
    k_msgq_put(&ibeacon_msg_queue, &node, K_NO_WAIT);
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv,
};

/* ========================================================================== */
/* Observer Control                                                           */
/* Registers the scan callback once, then starts or stops passive scanning.   */
/* ========================================================================== */

/* Registers the scan callback with the BT stack. */
static void observer_register_callbacks(void)
{
    if (!isRegistered) {
        int err = bt_le_scan_cb_register(&scan_callbacks);
        if (err < 0) {
            printk("[ERROR] Callback register failed (err: %d)\n", err);
            return;
        }

        isRegistered = true;
        printk("[INFO] Registered scan callbacks\n");
    }
}

/* Start the base chip scanning for iBeacon's (LISTEN_MODE) */
int observer_start(void)
{
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    observer_register_callbacks();
    int err = bt_le_scan_start(&scan_param, NULL);
    if (err) {
        printk("[ERROR] Start scanning failed (err %d)\n", err);
        return (err);
    }

    printk("[INFO] Sniffing for iBeacons\n");
    return (0);
}

/* Stop the base chip from scanning and re-enable GATT connection (STANDARD_MODE) */
int observer_stop(void)
{
    int err = bt_le_scan_stop();
    if (err < 0) {
        printk("[ERROR] Stop scanning failed (err: %d)\n", err);
        return (err);
    }

    // Unregister bad callbacks
    bt_le_scan_cb_unregister(&scan_callbacks);
    isRegistered = false;

    printk("[INFO] Sniffing Stopped\n");
    return (0);
}
