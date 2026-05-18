/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gatt.h"

#include "mac.h"
#include "common.h"
#include "rb_tree.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* ========================================================================== */
/* Function Prototypes                                                        */
/* ========================================================================== */

static int start_scan(void);
static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params);

/* ========================================================================== */
/* Per-Connection State                                                       */
/* ========================================================================== */

struct conn_state {
    /* Holds the active connection once we connect to the mobile */
    struct bt_conn *conn;

    /* Handle of the peripheral's NUS RX characteristic value.
     * Discovered during GATT discovery and used as the write target
     * in send_to_peripheral(). Zero until discovery completes. */
    uint16_t nus_rx_handle;

    /* Reused UUID buffer - gets overwritten at each stage of
     * discovery to tell bt_gatt_discover() what to look for next.
     * 128-bit to accommodate NUS UUIDs */
    struct bt_uuid_128 discover_uuid;

    /* Params passed to bt_gatt_discover() - must stay valid for
     * entire async discovery process, hence static */
    struct bt_gatt_discover_params discover_params;

    /* Params passed to bt_gatt_subscribe() - must stay valid for
     * the entire time we are subscribed, hence static */
    struct bt_gatt_subscribe_params subscribe_params;

    /* Params passed to bt_gatt_write() for sending data to the
     * peripheral's NUS RX characteristic - must stay valid until
     * the write callback fires */
    struct bt_gatt_write_params write_params;

    /* Atomic flag for closing ble connection intentionally */
    atomic_t intentional_disconnect;
};

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

static struct conn_state connections[NUM_CONNECTIONS] = {
    [0] = {.intentional_disconnect = ATOMIC_INIT(0)},
    [1] = {.intentional_disconnect = ATOMIC_INIT(0)},
};

K_MSGQ_DEFINE(gatt_msg_queue, sizeof(struct ble_packet), 8, 4);

/* ========================================================================== */
/* Helpers                                                                    */
/* ========================================================================== */

static int find_slot_by_con(struct bt_conn *conn)
{
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
        if (connections[i].conn == conn) {
            return i;
        }
    }

    return -1;
}

static int find_slot_by_addr(const bt_addr_le_t *addr)
{
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
        if (bt_addr_le_cmp(addr, &helm_addr[i]) == 0) {
            return i;
        }
    }

    return -1;
}

static int find_first_free_slot(void)
{
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
        if (connections[i].conn == NULL) {
            return i;
        }
    }

    return -1;
}

/* ========================================================================== */
/* NUS NOTIFICATION CALLBACK                                                  */
/* Fires every time the mobile sends a NUS notification.                      */
/* data == NULL indicates unsubscription (e.g. mobile disconnected).          */
/* Forwards received iBeacon structs to ibeacon_msg_queue.                    */
/* ========================================================================== */
static uint8_t notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    /* data == NULL means we got unsubscribed (e.g. helm_a/b disconnected) */
    if (!data) {
        printk("[WARN] Unsubscribed\n");
        params->value_handle = 0U;
        return (BT_GATT_ITER_STOP);
    }

    struct ble_packet *ble_packet = (struct ble_packet *)data;

    // Check the packet is valid
    if (ble_packet->crc16 != crc16_ansi((char *)&(ble_packet->data), sizeof(union ble_data))) {
        return (BT_GATT_ITER_CONTINUE);
    }

    // Pass on the valid packet
    k_msgq_put(&gatt_msg_queue, ble_packet, K_NO_WAIT);
    return (BT_GATT_ITER_CONTINUE); /* keep receiving notifications */
}

/* ========================================================================== */
/* WRITE CALLBACK                                                             */
/* Fires when a bt_gatt_write() to the peripheral's NUS RX characteristic     */
/* completes. Logs any errors.                                                */
/* ========================================================================== */
static void write_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(params);

    if (err) {
        printk("[WARN] Write failed %d\n", err);
    }
}

/* ========================================================================== */
/* SEND TO PERIPHERAL                                                         */
/* Writes data to a single peripheral's NUS RX characteristic.                */
/* Returns -ENOTCONN if the connection or RX handle is not yet ready.         */
/* ========================================================================== */
static int send_to_peripheral(int slot, uint8_t *data, uint16_t len)
{
    struct conn_state *cs = &connections[slot];
    if (!cs->conn || !cs->nus_rx_handle) {
        return (-ENOTCONN);
    }

    cs->write_params.data = data;
    cs->write_params.length = len;
    cs->write_params.handle = cs->nus_rx_handle;
    cs->write_params.func = write_cb;
    cs->write_params.offset = 0;

    return bt_gatt_write(cs->conn, &cs->write_params);
}

/* ========================================================================== */
/* SEND TO ALL                                                                */
/* Writes data to both peripheral's NUS RX characteristics.                   */
/* Returns early with error if either write fails.                            */
/* ========================================================================== */
int send_sync_pulse_to_helms(uint8_t *data, uint16_t len)
{
    int err1 = send_to_peripheral(0, data, len);
    int err2 = send_to_peripheral(1, data, len);
    // Send 1 failed
    if (err1 < 0) {
        return (err1);
    }

    // Send 2 failed
    if (err2 < 0) {
        return (err2);
    }

    // Both worked
    return (0);
}

/* ========================================================================== */
/* STEP 3: DISCOVER NUS SERVICE                                               */
/* Kicks off the discovery chain from the connected() callback.               */
/* Searches the full GATT database for the NUS primary service.               */
/* ========================================================================== */
void set_discover_nus_service(struct conn_state *cs)
{
    memcpy(&cs->discover_uuid, BT_UUID_NUS_SERVICE, sizeof(cs->discover_uuid));
    cs->discover_params.uuid = &cs->discover_uuid.uuid;
    cs->discover_params.func = discover_func;
    cs->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    cs->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    cs->discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(cs->conn, &cs->discover_params);
    if (err) {
        printk("[ERROR] Discover failed(err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 4: DISCOVER NUS TX CHARACTERISTIC                                     */
/* Called once the NUS service is found.                                      */
/* Searches within the service for the TX characteristic.                     */
/* ========================================================================== */
void set_discover_nus_tx(struct conn_state *cs, const struct bt_gatt_attr *attr)
{
    memcpy(&cs->discover_uuid, BT_UUID_NUS_TX_CHAR, sizeof(cs->discover_uuid));
    cs->discover_params.uuid = &cs->discover_uuid.uuid;
    cs->discover_params.start_handle = attr->handle + 1; /* search after service handle */
    cs->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(cs->conn, &cs->discover_params);
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
void set_discover_uuid_gatt_ccc(struct conn_state *cs, const struct bt_gatt_attr *attr)
{
    memcpy(&cs->discover_uuid, BT_UUID_GATT_CCC, sizeof(struct bt_uuid_16)); /* 16-bit source */
    cs->discover_params.uuid = &cs->discover_uuid.uuid;
    cs->discover_params.start_handle = attr->handle + 2; /* CCCD sits 2 handles after chrc */
    cs->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

    /* Save value handle now - needed later when subscribing */
    cs->subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

    int err = bt_gatt_discover(cs->conn, &cs->discover_params);
    if (err) {
        printk("[ERROR] Discover failed (err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 7: DISCOVER NUS RX CHARACTERISTIC                                     */
/* Called after subscribing to TX notifications.                              */
/* Searches for the NUS RX characteristic so the central can write to         */
/* the peripheral. Handle is saved in cs->nus_rx_handle on discovery.         */
/* ========================================================================== */
void set_discover_nus_rx(struct conn_state *cs, const struct bt_gatt_attr *attr)
{
    memcpy(&cs->discover_uuid, BT_UUID_NUS_RX_CHAR, sizeof(cs->discover_uuid));
    cs->discover_params.uuid = &cs->discover_uuid.uuid;
    cs->discover_params.start_handle = attr->handle + 1; /* search after service handle */
    cs->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(cs->conn, &cs->discover_params);
    if (err) {
        printk("[ERROR] Discover failed (err %d)\n", err);
    }
}

/* ========================================================================== */
/* STEP 6: SUBSCRIBE TO TX NOTIFICATIONS                                      */
/* Called once the CCCD is found.                                             */
/* Writes 0x0001 to the CCCD, telling the peripheral to start sending         */
/* notifications. notify_func fires on each received packet.                  */
/* Then chains into STEP 7 to discover the NUS RX characteristic.             */
/* ========================================================================== */
void set_discover_nus_sub(struct conn_state *cs, const struct bt_gatt_attr *attr)
{
    cs->subscribe_params.notify = notify_func;
    cs->subscribe_params.value = BT_GATT_CCC_NOTIFY; /* 0x0001 - enable notifications */
    cs->subscribe_params.ccc_handle = attr->handle;  /* the CCCD handle */

    int err = bt_gatt_subscribe(cs->conn, &cs->subscribe_params);
    if (err && err != -EALREADY) {
        printk("[ERROR] Subscribe failed (err %d)\n", err);
        return;
    } else {
        printk("[INFO] NUS Subscribed\n");
    }

    /* Now discover RX so we can write to the peripheral -> Step 7 */
    set_discover_nus_rx(cs, attr);
}

/* ========================================================================== */
/* DISCOVERY CHAIN CALLBACK                                                   */
/* Single callback reused for all discovery stages.                           */
/* Identifies current stage by comparing discover_params.uuid                 */
/* against what was being searched, then kicks off the next stage:            */
/*   NUS_SERVICE  -> STEP 4 (discover TX characteristic)                      */
/*   NUS_TX_CHAR  -> STEP 5 (discover CCCD descriptor)                        */
/*   GATT_CCC     -> STEP 6 (subscribe to TX notifications)                   */
/*   NUS_RX_CHAR  -> saves RX handle, discovery complete                      */
/* ========================================================================== */
static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{

    struct conn_state *cs = CONTAINER_OF(params, struct conn_state, discover_params);

    /* attr == NULL means this stage found nothing */
    if (!attr) {
        printk("Discover complete\n");
        (void)memset(params, 0, sizeof(*params));
        return (BT_GATT_ITER_STOP);
    }

    printk("[INFO] handle: %u\n", attr->handle);

    if (!bt_uuid_cmp(cs->discover_params.uuid, BT_UUID_NUS_SERVICE)) {
        set_discover_nus_tx(cs, attr); /* STEP 3 -> STEP 4 */
    } else if (!bt_uuid_cmp(cs->discover_params.uuid, BT_UUID_NUS_TX_CHAR)) {
        set_discover_uuid_gatt_ccc(cs, attr); /* STEP 4 -> STEP 5 */
    } else if (!bt_uuid_cmp(cs->discover_params.uuid, BT_UUID_GATT_CCC)) {
        set_discover_nus_sub(cs, attr); /* STEP 5 -> STEP 6 */
    } else if (!bt_uuid_cmp(cs->discover_params.uuid, BT_UUID_NUS_RX_CHAR)) {
        cs->nus_rx_handle = bt_gatt_attr_value_handle(attr);
        printk("[INFO] NUS rx Handle %u\n", cs->nus_rx_handle);
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
    // Find slot from address
    int target_slot = find_slot_by_addr(addr);
    if (target_slot < 0) {
        return;
    }

    // Already connected
    if (connections[target_slot].conn != NULL) {
        return;
    }

    // Not connectable, ignore
    if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_EXT_ADV) {
        return;
    }

    // Stop scanning to form the connection
    bt_le_scan_stop();

    // Try Coded PHY first (longer range), fall back to standard 1M PHY
    struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
    struct bt_conn_le_create_param *create_param = BT_CONN_LE_CREATE_CONN;

    create_param->options |= BT_CONN_LE_OPT_CODED;
    int err = bt_conn_le_create(addr, create_param, param, &connections[target_slot].conn);
    if (err < 0) {
        printk("[WARN] Coded PHY connection failed (err %d), trying 1M PHY\n", err);
        create_param->options &= ~BT_CONN_LE_OPT_CODED;
        err = bt_conn_le_create(addr, create_param, param, &connections[target_slot].conn);
        if (err < 0) {
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
static int start_scan(void)
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
    if (err < 0) {
        printk("[WARN] Scanning with Coded PHY support failed (err %d)\n", err);
        scan_param.options &= ~BT_LE_SCAN_OPT_CODED;
        err = bt_le_scan_start(&scan_param, device_found);
        if (err < 0) {
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

static struct bt_gatt_exchange_params mtu_exchange_params[NUM_CONNECTIONS] = {
    {.func = mtu_exchange_cb},
    {.func = mtu_exchange_cb},
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

    int slot = find_slot_by_con(conn);

    if (conn_err) {
        printk("[ERROR] Failed to connect to %s (%u)\n", addr, conn_err);
        bt_conn_unref(connections[slot].conn);
        connections[slot].conn = NULL;
        if (start_scan() < 0) {
            printk("[ERROR] Failed to resume scanning\n");
        }

        return;
    }

    printk("[INFO] Connected: %s\n", addr);

    rb_lock();
    struct helm_node *node = get_rb_node(slot);
    if (node) {
        node->connection_status = 0;
    }

    rb_unlock();

    // Request data length extension (over-the-air packet size)
    update_data_length(conn);

    // Request MTU exchange (GATT payload size)
    int err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params[slot]);
    if (err) {
        printk("[ERROR] MTU exchange request failed (err %d)", err);
    }

    /* kick off STEP 3 */
    set_discover_nus_service(&connections[slot]);
    if (find_first_free_slot() >= 0) {
        if (start_scan() < 0) {
            printk("[ERROR] Failed to resume scanning after partial connect\n");
        }
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

    int slot = find_slot_by_con(conn);
    if (slot < 0) {
        return;
    }

    bt_conn_unref(connections[slot].conn);
    connections[slot].conn = NULL;

    rb_lock();
    struct helm_node *node = get_rb_node(slot);
    if (node) {
        node->connection_status = 0;
    }

    rb_unlock();

    if (atomic_get(&connections[slot].intentional_disconnect)) {
        atomic_clear(&connections[slot].intentional_disconnect);
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

/* ========================================================================== */
/* CLOSE CONNECTION                                                           */
/* Unsubscribes from NUS notifications and disconnects from the given slot.   */
/* Sets intentional_disconnect flag to prevent automatic reconnection.        */
/* Cleanup of conn_state is handled in the disconnected() callback.           */
/* Returns -EINVAL for bad slot, 0 if already disconnected or on success.     */
/* ========================================================================== */
int close_connection(int slot)
{
    if (slot < 0 || slot >= NUM_CONNECTIONS) {
        return (-EINVAL);
    }

    if (connections[slot].conn == NULL) {
        return (0);
    }

    atomic_set(&connections[slot].intentional_disconnect, 1);
    bt_gatt_unsubscribe(connections[slot].conn, &connections[slot].subscribe_params);
    int err = bt_conn_disconnect(connections[slot].conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        printk("[ERROR] Disconnect failed (err %d)", err);
        atomic_clear(&connections[slot].intentional_disconnect);
        return (err);
    }

    return (0);
}

/* ========================================================================== */
/* SET STATIC ADDRESS                                                         */
/* Creates a BT identity using the hardcoded base_addr.                       */
/* Must be called before bt_enable() to ensure the static MAC address is      */
/* used for all advertising and connections, preventing RPA rotation from     */
/* breaking MAC-based filtering and whitelisting on the peripherals.          */
/* ========================================================================== */
static int set_static_address(void)
{
    int err = bt_id_create((bt_addr_le_t *)&base_addr, NULL);
    if (err < 0) {
        printk("Failed to create identity: %d\n", err);
        return err;
    }

    return 0;
}

int initialise_base_gatt(void)
{
    int err = set_static_address();
    if (err < 0) {
        printk("[ERROR] Failed to set static MAC\n");
        return (err);
    }

    err = bt_enable(NULL);
    if (err) {
        printk("[ERROR] Bluetooth init failed (err %d)\n", err);
        return (err);
    }

    printk("[INFO] Bluetooth initialized\r\n");

    err = start_scan(); /* STEP 1 for GATT process */
    if (err < 0) {
        printk("Initial scan failed (err %d)\n", err);
        return (err);
    }

    return 0;
}
