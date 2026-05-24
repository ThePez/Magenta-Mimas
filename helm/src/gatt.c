/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gatt.h"

#include "imu.h"
#include "mac.h"
#include "common.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define ACK_TIMEOUT K_SECONDS(10)

/* ========================================================================== */
/* Workqueues for timeout/readvertising                                       */
/* ========================================================================== */

static struct k_work adv_restart_work;
static struct k_work_delayable timeout_work;

K_SEM_DEFINE(notif_sem, 0, 1);

/* ========================================================================== */
/* Current BLE connection                                                     */
/* ========================================================================== */

static struct bt_conn *current_conn = NULL;
static struct bt_le_adv_param adv_param;
static uint64_t connect_time = 0;

/* ========================================================================== */
/* Advertising Data                                                           */
/* ========================================================================== */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

/* ========================================================================== */
/* Static MAC                                                                 */
/* ========================================================================== */

static int set_static_address(void)
{
#ifndef NODE_ID
#error "NODE ID not found"
#endif

    int slot = NODE_ID;

    int err = bt_id_create((bt_addr_le_t *)&helm_addr[slot], NULL);
    if (err < 0) {
        printk("[ERROR] Failed to create identity: %d\n", err);
        return err;
    }

    return 0;
}

#ifdef WHITELIST
/* ========================================================================== */
/* Whitelist / Filter Accept List                                             */
/* ========================================================================== */

static int configure_accept_list(void)
{
    int err = bt_le_filter_accept_list_clear();
    if (err) {
        printk("[ERROR] Failed to clear accept list (err %d)\n", err);
        return err;
    }

    err = bt_le_filter_accept_list_add(&base_addr);
    if (err) {
        printk("[ERROR] Failed to add base to accept list (err %d)\n", err);
        return err;
    }

    printk("[INFO] Accept list configured\n");
    return 0;
}
#endif

/* ========================================================================== */
/* NUS Callbacks                                                              */
/* ========================================================================== */

static void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(enabled);
}

static void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(conn);
    ARG_UNUSED(len);

    struct cmd_ble_packet *packet = (struct cmd_ble_packet *)data;
    if (packet->cmd == 1) {
        set_absolute_time(packet->time);
        printk("[INFO] TIME SET %lld\n", packet->time);
    }

    k_sem_give(&notif_sem);
    k_work_reschedule(&timeout_work, ACK_TIMEOUT);
}

struct bt_nus_cb nus_listener = {
    .notif_enabled = notif_enabled,
    .received = received,
};

/* ========================================================================== */
/* BLE Gatt Disconnection timeout                                             */
/* ========================================================================== */

static void disconnect_timeout(struct k_work *work)
{
    if (current_conn != NULL) {
        printk("[INFO] Connection timeout, disconnecting\n");
        bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}

static void adv_restart(struct k_work *work)
{
    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err < 0) {
        printk("[ERROR] Failed to restart advertising (err %d)\n", err);
        return;
    }

    printk("[INFO] NUS Advertising restarted\n");
}

/* ========================================================================== */
/* BLE Gatt Connection and Disconnection callbacks                            */
/* ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }

    current_conn = bt_conn_ref(conn);
    connect_time = k_uptime_get();
    printk("[INFO] Connected\n");

    // Turn on the IMU now that ble is connected
    resume_imu();

    /* Start timeout watchdog - 10 seconds */
    k_work_reschedule(&timeout_work, ACK_TIMEOUT);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    uint64_t duration = k_uptime_get() - connect_time;
    printk("[WARN] Disconnected (reason 0x%02x) after %llu ms\n", reason, duration);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    k_work_cancel_delayable(&timeout_work);

    // Turn off the IMU as ble is not connected anymore
    susspend_imu();

    /* Restart advertising so base can reconnect */
    k_work_submit(&adv_restart_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* ========================================================================== */
/* MTU callbacks                                                              */
/* ========================================================================== */

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    // Display new MTU values (buffer size for NUS)
    printk("[INFO] Updated MTU, TX: %d, RX: %d\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

/* ========================================================================== */
/* GATT intialisation procedure                                               */
/* ========================================================================== */

int initialise_helm_gatt(void)
{
    if (set_static_address()) {
        printk("[ERROR] Failed to set static MAC\n");
        return (-1);
    }

    // Register NUS callbacks before bt_enable
    int err = bt_nus_cb_register(&nus_listener, NULL);
    if (err) {
        printk("[ERROR] Failed to register NUS callback: %d\n", err);
        return (err);
    }

    // These are added to the default Zephyr System Work Queue
    // Delayed work task for restarting ble advertising
    k_work_init(&adv_restart_work, adv_restart);
    // Delayed timer task to auto disconnect the GATT if no NUS sends occur in 10s
    k_work_init_delayable(&timeout_work, disconnect_timeout);

    // Enable bluetooth
    err = bt_enable(NULL);
    if (err) {
        printk("[ERROR] Failed to enable bluetooth: %d\n", err);
        return (err);
    }

    // Setup the NUS ad params
    adv_param = *BT_LE_ADV_CONN_FAST_1;

#ifdef WHITELIST
    // Accept list must be configured after bt_enable
    if (configure_accept_list()) {
        printk("[ERROR] Failed to configure accept list\n");
        return (-1);
    }

    // Add white-list options if filtering
    adv_param.options |= BT_LE_ADV_OPT_FILTER_CONN | BT_LE_ADV_OPT_FILTER_SCAN_REQ;
#endif

    // New MTU size
    bt_gatt_cb_register(&gatt_callbacks);

    // Start NUS advertising so base chip can connect
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("[ERROR] Failed to start advertising: %d\n", err);
        return (err);
    }

    return 0;
}

uint8_t am_i_connected(void)
{
    return current_conn != NULL;
}

int send_data_nus(const void *data, uint16_t len)
{
    if (current_conn == NULL) {
        return (-1);
    }

    int ret = bt_nus_send(NULL, data, len);
    if (ret == 0) {
        k_work_reschedule(&timeout_work, ACK_TIMEOUT);
    } else {
        printk("[ERROR] Failed to send packet: %d\nPointer: %p Size: %d\n", ret, data, len);
    }

    return (ret);
}
