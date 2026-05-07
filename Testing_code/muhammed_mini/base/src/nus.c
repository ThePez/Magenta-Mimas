/*
 * Copyright (c) 2026 Sam Kwort
 * Changed by: Muhammed Abdilrahmin
 */

// Foreword:
// A LOT of this code is very similar to the original copy provided by Sam
// Kwort - some of the modifications include removing the ability to transmit
// data to the mobile node, and altering the scan function such that it only 
// connects to mobile nodes with the name "MA,JB" (for Muhammed Abdilrahmin, 
// Jack Boyd, I think). 

#include "sniffer.h"
#include "nus.h"

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

/* ========================================================================== */
/* Memory management                                                          */
/* ========================================================================== */

#define HEAP_SIZE   1536
#define MS_TO_BT(x) ((x) * 8 / 5)

// Creating a heap and a FIFO to store the messages received from NUS on - 
// these are fixed size messages (essentially).
K_FIFO_DEFINE(nus_package_fifo);
K_HEAP_DEFINE(nus_package_heap, HEAP_SIZE);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define MOBILE_NAME "MA,JB"

static const struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_ACTIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = MS_TO_BT(100),
    .window = MS_TO_BT(100)};

static const struct bt_conn_le_data_len_param dl_param = {
    .tx_max_len = BT_GAP_DATA_LEN_MAX, .tx_max_time = BT_GAP_DATA_TIME_MAX};

int start_scan(void);

/* ========================================================================== */
/* State                                                                      */
/* ========================================================================== */

static struct bt_conn *default_conn;

static struct bt_uuid_128 discover_uuid;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

/* ==========================================================================
 * GATT Notification Callback
 * ========================================================================== */

// This function is called on a notification...
static uint8_t notify_func(struct bt_conn *conn,
                           struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    if (!data) {
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    struct nus_package *package =
        k_heap_alloc(&nus_package_heap, sizeof(struct nus_package), K_NO_WAIT);
    const char *bytes = data;
    uint16_t msg_length = MIN(length, NUS_MAX_MTU - 1);

    if (package != NULL) {
        // Don't allocate unnecessarily huge amounts of space!
        package->data = k_heap_alloc(
            &nus_package_heap, sizeof(char) * (msg_length + 1), K_NO_WAIT);
        if (package->data == NULL) {
            printk("L: Error allocating space! - Dropping packet\n");
            return BT_GATT_ITER_CONTINUE;
        }

        memcpy(package->data, bytes, msg_length);
        package->data[msg_length] = '\0';

        k_fifo_put(&nus_package_fifo, package);

        return BT_GATT_ITER_CONTINUE;
    }

    // We don't want to kill the process if there isn't enough memory, maybe
    // it's just a MUTEX that's not unlocking for whatever reason.
    printk("L: Error allocating space! - Dropping packet\n");
    return BT_GATT_ITER_CONTINUE;
}

/* ==========================================================================
 * GATT Discovery Callback
 * ========================================================================== */

// Once you get a connection, you have to discover its services by comparing
// the UUIDs against what you're looking for:
static uint8_t discover_func(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    if (!attr) {
        memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    // Look for the NUS service first... You have to do this step by step. The
    // first if statement runs the first time, then the else if, then the else
    // depending on where it is in the discovery process. 
    if (!bt_uuid_cmp(discover_params.uuid,
                     BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL))) {
        memcpy(&discover_uuid, BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL),
               sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        bt_gatt_discover(conn, &discover_params);
    } else if (!bt_uuid_cmp(discover_params.uuid,
                            BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL))) {
        // Otherwise, look for the TX service...
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        /* Now find the CCC descriptor */
        memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(struct bt_uuid_16));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

        bt_gatt_discover(conn, &discover_params);
    } else {
        subscribe_params.notify = notify_func;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        // Subscribe to the peripheral's notifications!
        bt_gatt_subscribe(conn, &subscribe_params);
    }

    return BT_GATT_ITER_STOP;
}

/* ========================================================================== */
/* Scanning                                                                   */
/* ========================================================================== */

// Checking if the advertising packet has the NUS service UUID in it
static bool nus_uuid_cb(struct bt_data *data, void *user_data)
{
    bool *found = user_data;

    if (data->type == BT_DATA_UUID128_ALL ||
        data->type == BT_DATA_UUID128_SOME) {
        /* Each entry is 16 bytes (128-bit UUID) */
        if (data->data_len % 16 != 0) {
            return true;
        }

        for (uint16_t i = 0; i < data->data_len; i += 16) {
            struct bt_uuid_128 uuid;

            if (!bt_uuid_create(&uuid.uuid, &data->data[i], 16)) {
                continue;
            }

            if (!bt_uuid_cmp(&uuid.uuid,
                             BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL))) {
                *found = true;
                return false;
            }
        }
    }

    return true;
}

// Checking if the advertising packet has the mobile node name in it
static bool nus_name_cb(struct bt_data *data, void *user_data)
{
    // Ugly name, but it makes sense, right?
    bool *majb = user_data;

    if (data->type == BT_DATA_NAME_COMPLETE ||
        data->type == BT_DATA_NAME_SHORTENED) {
        uint16_t data_len = MIN(data->data_len, strlen(MOBILE_NAME));

        if (strncmp(data->data, MOBILE_NAME, data_len) == 0) {
            *majb = true;
            return false;
        }
    }

    return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                         struct net_buf_simple *ad)
{
    struct net_buf_simple_state ad_state;
    net_buf_simple_save(ad, &ad_state);

    // Check if the sniffer runs - if it doesn't there's no need to do the 
    // restore.
    if (sniffer_write(addr, rssi, type, ad)) {
        net_buf_simple_restore(ad, &ad_state);
    }

    // We are already connected, return!
    if (default_conn) {
        return;
    }

    if (type != BT_GAP_ADV_TYPE_ADV_IND &&
        type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_SCAN_RSP) {
        return;
    }

    bool has_name = false, has_nus = false;

    // Check if it has the name (simple) before doing the UUID comparison
    // (expensive)
    bt_data_parse(ad, nus_name_cb, &has_name);

    if (!has_name) {
        return;
    }

    net_buf_simple_restore(ad, &ad_state);
    bt_data_parse(ad, nus_uuid_cb, &has_nus);

    if (!has_nus) {
        return;
    }

    // We don't *really* need this since it reconnects on connection, but
    // bt_conn_le_create stops advertising anyway, so there's really no harm
    // to have this here
    if (bt_le_scan_stop()) {
        return;
    }

    // Create the connection and restart scanning if it fails (this will be our
    // new default_conn)
    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT, &default_conn);
    if (err) {
        start_scan();
    }
}

/* ========================================================================== */
/* MTU Length Extension and MTU Exchange                                      */
/* ========================================================================== */

// This has to be here just to make mtu_exchange_params happy...
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params)
{
    return;
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_cb,
};

/* ========================================================================== */
/* Connection Callbacks                                                       */
/* ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    // If there's a connection error, just reset the connection and restart
    // scanning.
    if (conn_err) {
        bt_conn_unref(default_conn);
        default_conn = NULL;

        start_scan();
        return;
    }

    // Thanks, Sam! 

    /* Request data length extension (over-the-air packet size) */
    bt_conn_le_data_len_update(conn, &dl_param);
    /* Request MTU exchange (GATT payload size) */
    bt_gatt_exchange_mtu(conn, &mtu_exchange_params);

    /* Start discovering NUS service */
    memcpy(&discover_uuid, BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL),
           sizeof(discover_uuid));

    discover_params.uuid = &discover_uuid.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    bt_gatt_discover(conn, &discover_params);

    // Start scanning on the connection rather than the disconnection, since 
    // we need to scan for both NUS and sniffer purposes.
    start_scan();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    // Disconnected - but this shouldn't have been connected anyway!?
    if (default_conn != conn) {
        return;
    }

    bt_conn_unref(default_conn);
    default_conn = NULL;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {.connected = connected,
                                     .disconnected = disconnected};

/* ========================================================================== */
/* Header definitions                                                         */
/* ========================================================================== */

int start_scan(void)
{
    // We only ever manually start scanning once - the sniffer mode is just
    // an atomic that runs a function in scan_found. Turning off/on scanning
    // caused issues with segfaults and memory overwrites that caused the device
    // to crash after switching modes (even once!)
    return bt_le_scan_start(&scan_param, device_found);
}
