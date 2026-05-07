/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "base_gatt.h"
#include "observer.h"

#include <stdint.h>

#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/nus.h>
#include "zephyr/bluetooth/services/nus/inst.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>

/* ========================================================================== */
/* Function Prototypes                                                        */
/* ========================================================================== */

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params);

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

/* Mobile chip address - hardcoded for filtering */
static const bt_addr_le_t mobile_addr = {
    .type = BT_ADDR_LE_RANDOM, .a.val = {0x56, 0x63, 0xCD, 0x44, 0x4A, 0xE1}
    // E1:4A:44:CD:63:56
};

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

/* Holds the active connection once we connect to the mobile */
static struct bt_conn *mobile_connection;

/* Reused UUID buffer - gets overwritten at each stage of
 * discovery to tell bt_gatt_discover() what to look for next.
 * 128-bit to accommodate NUS UUIDs */
static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);

/* Params passed to bt_gatt_discover() - must stay valid for
 * entire async discovery process, hence static */
static struct bt_gatt_discover_params discover_params;

/* Params passed to bt_gatt_subscribe() - must stay valid for
 * the entire time we are subscribed, hence static */
static struct bt_gatt_subscribe_params subscribe_params;

/* Atomic flag for closing ble connection intentionally */
static atomic_t intentional_disconnect = ATOMIC_INIT(0);

K_MSGQ_DEFINE(ibeacon_msg_queue, sizeof(struct iBeacon), 60, 4);

/* ========================================================================== */
/* NUS NOTIFICATION CALLBACK                                                  */
/* Fires every time the mobile sends a NUS notification.                      */
/* data == NULL indicates unsubscription (e.g. mobile disconnected).          */
/* Forwards received iBeacon structs to ibeacon_msg_queue.                   */
/* ========================================================================== */
static uint8_t notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    /* data == NULL means we got unsubscribed (e.g. mobile disconnected) */
    if (!data) {
        printk("[WARN] Unsubscribed\n");
        params->value_handle = 0U;
        return (BT_GATT_ITER_STOP);
    }

    struct iBeacon *beacon = (struct iBeacon *)data;
    /* Send the iBeacon Data to the message queue */
    k_msgq_put(&ibeacon_msg_queue, beacon, K_NO_WAIT);
    return (BT_GATT_ITER_CONTINUE); /* keep receiving notifications */
}

/* ========================================================================== */
/* STEP 3: DISCOVER NUS SERVICE                                               */
/* Kicks off the discovery chain from the connected() callback.               */
/* Searches the full GATT database for the NUS primary service.               */
/* ========================================================================== */
void set_discover_nus_service(void)
{
    memcpy(&discover_uuid, BT_UUID_NUS_SERVICE, sizeof(discover_uuid));
    discover_params.uuid = &discover_uuid.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(mobile_connection, &discover_params);
    if (err) {
        printk("[ERROR] Discover failed(err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 4: DISCOVER NUS TX CHARACTERISTIC                                     */
/* Called once the NUS service is found.                                      */
/* Searches within the service for the TX characteristic.                     */
/* ========================================================================== */
void set_discover_nus_tx(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    memcpy(&discover_uuid, BT_UUID_NUS_TX_CHAR, sizeof(discover_uuid));
    discover_params.uuid = &discover_uuid.uuid;
    discover_params.start_handle = attr->handle + 1; /* search after service handle */
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        printk("[ERROR] Discover failed (err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 5: DISCOVER CCCD DESCRIPTOR                                           */
/* Called once the TX characteristic is found.                                */
/* Saves the value handle, then finds the CCCD descriptor.                    */
/* attr->handle = declaration handle                                          */
/* value_handle = actual data handle used for subscription                    */
/* ========================================================================== */
void set_discover_uuid_gatt_ccc(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(struct bt_uuid_16)); /* 16-bit source */
    discover_params.uuid = &discover_uuid.uuid;
    discover_params.start_handle = attr->handle + 2; /* CCCD sits 2 handles after chrc */
    discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

    /* Save value handle now - needed later when subscribing */
    subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

    int err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        printk("[ERROR] Discover failed (err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 6: SUBSCRIBE TO TX NOTIFICATIONS                                      */
/* Called once the CCCD is found.                                             */
/* Writes 0x0001 to the CCCD, telling the mobile to start                     */
/* sending notifications. notify_func fires on each packet.                   */
/* ========================================================================== */
void set_discover_nus_sub(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    subscribe_params.notify = notify_func;
    subscribe_params.value = BT_GATT_CCC_NOTIFY; /* 0x0001 - enable notifications */
    subscribe_params.ccc_handle = attr->handle;  /* the CCCD handle */

    int err = bt_gatt_subscribe(conn, &subscribe_params);
    if (err && err != -EALREADY) {
        printk("[ERROR] Subscribe failed (err %d)\n", err);
    } else {
        printk("[INFO] NUS Subscribed\n");
    }
}

/* ========================================================================== */
/* DISCOVERY CHAIN CALLBACK                                                   */
/* Single callback reused for all discovery stages.                           */
/* Identifies current stage by comparing discover_params.uuid                 */
/* against what was being searched, then kicks off next stage.                */
/* ========================================================================== */
static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    /* attr == NULL means this stage found nothing */
    if (!attr) {
        printk("Discover complete\n");
        (void)memset(params, 0, sizeof(*params));
        return (BT_GATT_ITER_STOP);
    }

    printk("[INFO] handle: %u\n", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_NUS_SERVICE)) {
        set_discover_nus_tx(conn, attr); /* STEP 3 -> STEP 4 */
    } else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_NUS_TX_CHAR)) {
        set_discover_uuid_gatt_ccc(conn, attr); /* STEP 4 -> STEP 5 */
    } else {
        set_discover_nus_sub(conn, attr); /* STEP 5 -> STEP 6 */
        return (BT_GATT_ITER_STOP);
    }

    return (BT_GATT_ITER_STOP);
}

/* ========================================================================== */
/* STEP 2: DEVICE FOUND DURING SCAN                                           */
/* Fires for every advertising packet seen during scanning.                   */
/* Filters by address then attempts connection to mobile chip.                */
/* ========================================================================== */
static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                         struct net_buf_simple *ad)
{
    /* Not mobile chip, ignore */
    if (bt_addr_le_cmp(addr, &mobile_addr) != 0) {
        return;
    }

    /* Not connectable, ignore */
    if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_EXT_ADV) {
        return;
    }

    char dev[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, dev, sizeof(dev));
    printk("[INFO] device: %s, AD evt type %u, AD data len %u, RSSI %i\n", dev, type, ad->len,
           rssi);

    /* Stop scanning before making a connection */
    if (bt_le_scan_stop()) {
        return;
    }

    /* Try Coded PHY first (longer range), fall back to standard 1M PHY */
    struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
    struct bt_conn_le_create_param *create_param = BT_CONN_LE_CREATE_CONN;

    create_param->options |= BT_CONN_LE_OPT_CODED;
    int err = bt_conn_le_create(addr, create_param, param, &mobile_connection);
    if (err) {
        printk("[WARN] Coded PHY connection failed (err %d), trying 1M PHY\n", err);
        create_param->options &= ~BT_CONN_LE_OPT_CODED;
        err = bt_conn_le_create(addr, create_param, param, &mobile_connection);
        if (err) {
            printk("[ERROR] Create connection failed (err %d)\n", err);
            if (start_scan() < 0) {
                printk("[ERROR] Failed to resume scanning\n");
            }
        }
    }
}

/* ========================================================================== */
/* STEP 1: START SCANNING                                                     */
/* Also called again after disconnection to resume scanning.                  */
/* ========================================================================== */
int start_scan(void)
{
    int err;

    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_CODED,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    /* Try with Coded PHY first, fall back to standard 1M PHY */
    err = bt_le_scan_start(&scan_param, device_found);
    if (err) {
        printk("[WARN] Scanning with Coded PHY support failed (err %d)\n", err);
        scan_param.options &= ~BT_LE_SCAN_OPT_CODED;
        err = bt_le_scan_start(&scan_param, device_found);
        if (err) {
            printk("[ERROR] Scanning failed to start (err %d)\n", err);
            return (err);
        }
    }

    printk("[INFO] Looking for GATT connection\n");
    return (0);
}

/* ========================================================================== */
/* Data Length Extension and MTU Exchange                                     */
/*                                                                            */
/* These are two separate negotiations:                                       */
/*   1. DLE  -- over-the-air packet size (bt_conn_le_data_len_update)         */
/*   2. MTU  -- GATT payload size        (bt_gatt_exchange_mtu)               */
/* Both must be requested; each result is min(ours, theirs).                  */
/* ========================================================================== */

static void update_data_length(struct bt_conn *conn)
{
    int err;
    struct bt_conn_le_data_len_param dl_param = {
        .tx_max_len = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };

    err = bt_conn_le_data_len_update(conn, &dl_param);
    if (err) {
        printk("[ERROR] Data length update failed (err %d)", err);
    }
}

static void on_le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
    printk("Data length updated: TX %u bytes (%u us), RX %u bytes (%u us)", info->tx_max_len,
           info->tx_max_time, info->rx_max_len, info->rx_max_time);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params)
{
    if (err) {
        printk("[ERROR] MTU exchange failed (err %u)", err);
    } else {
        uint16_t payload_mtu = bt_gatt_get_mtu(conn) - 3;
        printk("MTU exchange successful: ATT MTU %u, payload %u bytes", bt_gatt_get_mtu(conn),
               payload_mtu);
    }
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_cb,
};

/* ========================================================================== */
/* EVENT: CONNECTED                                                           */
/* Fired by the BT stack when a connection is established.                    */
/* Kicks off the GATT discovery chain.                                        */
/* ========================================================================== */
static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
        printk("[ERROR] Failed to connect to %s (%u)\n", addr, conn_err);
        bt_conn_unref(mobile_connection);
        mobile_connection = NULL;
        if (start_scan() < 0) {
            printk("[ERROR] Failed to resume scanning\n");
        }
        return;
    }

    printk("[INFO] Connected: %s\n", addr);

    /* Request data length extension (over-the-air packet size) */
    update_data_length(conn);

    /* Request MTU exchange (GATT payload size) */
    int err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
    if (err) {
        printk("[ERROR] MTU exchange request failed (err %d)", err);
    }

    if (conn == mobile_connection) {
        set_discover_nus_service(); /* kick off STEP 3 */
    }
}

/* ========================================================================== */
/* EVENT: DISCONNECTED                                                        */
/* Fired when connection drops for any reason.                                */
/* Cleans up and restarts scanning to reconnect.                              */
/* ========================================================================== */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("[WARN] Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

    if (mobile_connection != conn) {
        return;
    }

    bt_conn_unref(mobile_connection);
    mobile_connection = NULL;

    if (atomic_get(&intentional_disconnect)) {
        atomic_clear(&intentional_disconnect);
        printk("[WARN] Intentional disconnect, not reconnecting.\n");
        return;
    }

    if (start_scan() < 0) {
        printk("[ERROR] Failed to resume scanning\n");
    }
}

/* ========================================================================== */
/* Register connection callbacks with the BT stack                            */
/* ========================================================================== */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_data_len_updated = on_le_data_len_updated,
};

/* Unsubscribe and close the Gatt connection. Clean-up done in disconnect callback function */
int close_connection(void)
{
    if (mobile_connection == NULL) {
        return (0);
    }

    atomic_set(&intentional_disconnect, 1);
    bt_gatt_unsubscribe(mobile_connection, &subscribe_params);
    int err = bt_conn_disconnect(mobile_connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        printk("[ERROR] Disconnect failed (err %d)", err);
        atomic_clear(&intentional_disconnect);
        return (err);
    }

    return (0);
}
