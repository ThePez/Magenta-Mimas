/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "observer.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>

/* ========================================================================== */
/* NUS Callbacks                                                              */
/* ========================================================================== */

static void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

struct bt_nus_cb nus_listener = {
    .notif_enabled = notif_enabled,
    // no .received needed - mobile doesn't care about incoming data
};

/* ========================================================================== */
/* Advertising Data                                                           */
/* ========================================================================== */

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

/* ========================================================================== */
/* BLE Gatt Conection                                                         */
/* ========================================================================== */

static struct bt_conn *current_conn = NULL;
static struct k_work adv_restart_work;
static uint64_t connect_time = 0;

static void disconnect_timeout(struct k_work *work)
{
    if (current_conn != NULL) {
        printk("Connection timeout, disconnecting\n");
        bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}

static void adv_restart(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err < 0) {
        printk("[ERROR] Failed to restart advertising (err %d)\n", err);
        return;
    }
    
    printk("Advertising restarted\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }

    current_conn = bt_conn_ref(conn);
    connect_time = k_uptime_get();
    printk("Connected\n");
    /* Start timeout watchdog - 10 seconds */
    k_work_reschedule(&timeout_work, K_SECONDS(10));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    uint64_t duration = k_uptime_get() - connect_time;
    printk("Disconnected (reason 0x%02x) after %llu ms\n", reason, duration);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    k_work_cancel_delayable(&timeout_work);

    /* Restart advertising so base can reconnect */
    k_work_submit(&adv_restart_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("CSSE4011 Mini-Project Mobile Chip\r\n");

    int err;

    // Register NUS callbacks before bt_enable
    err = bt_nus_cb_register(&nus_listener, NULL);
    if (err) {
        printk("[ERROR] Failed to register NUS callback: %d\n", err);
        return err;
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
        return err;
    }

    // Start NUS advertising so base chip can connect
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("[ERROR] Failed to start advertising: %d\n", err);
        return err;
    }

    // Start scanning for iBeacons
    observer_start();
    printk("Initialization complete\n");
    return 0;
}
