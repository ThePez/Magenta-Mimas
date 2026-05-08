/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>

#define CHIP_A // Swaps the name

#ifdef CHIP_A
#define DEVICE_NAME "Helm-A"
#else
#define DEVICE_NAME "Helm-B"
#endif

#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static struct k_work adv_restart_work;
struct k_work_delayable timeout_work;

static struct bt_conn *current_conn = NULL;
static uint64_t connect_time = 0;

static const bt_addr_le_t base_addr = {
    .type = BT_ADDR_LE_RANDOM, .a.val = {0xBB, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
    // FF:EE:DD:CC:BB:BB  <-- replace with Base address
};

/* Helm chip addresses - hardcoded for filtering */
static const bt_addr_le_t helm_addr[2] = {
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

int set_static_address(void)
{
#ifdef CHIP_A
    int slot = 0;
#else
    int slot = 1;
#endif

    int err = bt_id_create((bt_addr_le_t *)&helm_addr[slot], NULL);
    if (err < 0) {
        printk("Failed to create identity: %d\n", err);
        return err;
    }

    return 0;
}

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
/* NUS Callbacks                                                              */
/* ========================================================================== */

static void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

static void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(conn);

    const char *data_char = (const char *)data;
    printk("Data received:");

    for (uint16_t i = 0; i < len; i++) {
        printk(" %02X", data_char[i]);
    }

    printk("\n");
}

struct bt_nus_cb nus_listener = {
    .notif_enabled = notif_enabled,
    .received = received,
};

/* ========================================================================== */
/* BLE Gatt Conection                                                         */
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
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err < 0) {
        printk("[ERROR] Failed to restart advertising (err %d)\n", err);
        return;
    }

    printk("[INFO] NUS Advertising restarted\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }

    current_conn = bt_conn_ref(conn);
    connect_time = k_uptime_get();
    printk("[INFO] Connected\n");
    /* Start timeout watchdog - 10 seconds */
    k_work_reschedule(&timeout_work, K_SECONDS(10));
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

    /* Restart advertising so base can reconnect */
    k_work_submit(&adv_restart_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// Display new MTU values (buffer size for NUS)
static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    printk("[INFO] Updated MTU, TX: %d, RX: %d\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    printk("CSSE4011 Project %s Chip\r\n", DEVICE_NAME);

    if (set_static_address()) {
        printk("[ERROR] Failed to set static MAC\n");
        return (-1);
    }

    // Register NUS callbacks before bt_enable
    int err = bt_nus_cb_register(&nus_listener, NULL);
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

    // New MTU size
    bt_gatt_cb_register(&gatt_callbacks);

    // Start NUS advertising so base chip can connect
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("[ERROR] Failed to start advertising: %d\n", err);
        return err;
    }

    printk("[INFO] Network initialization complete\n");

    const char *const data = "hello world";
    while (1) {
        // Send via NUS
        int ret = bt_nus_send(NULL, data, strlen(data));
        if (ret == 0) {
            k_work_reschedule(&timeout_work, K_SECONDS(10));
        }

        k_msleep(500);
    }
}

/*



*/
