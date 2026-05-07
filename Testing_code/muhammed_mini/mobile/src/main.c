/* ========================================================================== */
/* Main mobile file - advertises to base node as "MA,JB" and hopefully        */
/* connects as appropriate. Gets beacon data and sends it out over NUS 251    */
/* bytes at a time.                                                           */
/* Written: Muhammed A                                                        */
/* ========================================================================== */

#include <stdio.h>
#include <strings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/kernel.h>

#define DEVICE_NAME     "MA,JB"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define IBEACON_ID_LEN  4

#define NUS_MAX_MTU    247
#define NUS_MAX_BUFFER 18
#define NUS_ITEM_COUNT (NUS_MAX_MTU / NUS_MAX_BUFFER)

#define MS_TO_BT(x) ((x) * 8 / 5)

/* ========================================================================== */
/* Struct Decs.                                                               */
/* ========================================================================== */

// Major, Minor and RSSI are the only items being sent.
struct bt_package {
    bool isIBeacon;
    uint16_t major;
    uint16_t minor;
};

/* ========================================================================== */
/* Constant Decs.                                                             */
/* ========================================================================== */

// Used for filtering.
static const char iBeaconId[IBEACON_ID_LEN] = {0x4c, 0x00, 0x02, 0x15};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL)};

static const struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_ACTIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = MS_TO_BT(100),
    .window = MS_TO_BT(100)};

/* ========================================================================== */
/* State Decs.                                                                */
/* ========================================================================== */

// Since the buffer is being fed to a large function (json) it is good to keep
// it in a mutex, just in case the scan callback overwrites it.
K_MUTEX_DEFINE(nus_buffer_mutex);

static char nus_buffer[NUS_MAX_MTU];
// Fill buffer up to NUS_ITEM_COUNT before sending.
static atomic_t buffer_count = ATOMIC_INIT(0);

/* ========================================================================== */
/* Beacon Detection                                                           */
/* ========================================================================== */

// Only filter out iBeacons and extract maj + min
static bool data_cb(struct bt_data *data, void *user_data)
{
    struct bt_package *pkg = user_data;

    switch (data->type) {
    case BT_DATA_MANUFACTURER_DATA:

        uint16_t len = data->data_len;
        if (len > 5) {
            for (int i = 0; i < IBEACON_ID_LEN; i++) {
                if (iBeaconId[i] != data->data[i]) {
                    return false;
                }
            }

            pkg->isIBeacon = true;
            pkg->major = (data->data[len - 5] << 8) | (data->data[len - 4]);
            pkg->minor = (data->data[len - 3] << 8) | (data->data[len - 2]);
        }
        return false;
    default:
        return true;
    }
}

// Scan callback, called when a device is found.
static void scan_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                       struct net_buf_simple *ad)
{
    // This one is only temporary.
    static char buffer[NUS_MAX_BUFFER];
    if (ad->len >= 1) {
        struct bt_package pkg;
        pkg.isIBeacon = false;

        bt_data_parse(ad, data_cb, &pkg);

        if (pkg.isIBeacon) {
            snprintf(buffer, NUS_MAX_BUFFER, "%d,%d,%d|", pkg.major, pkg.minor,
                     rssi);

            // Make sure this is the only place actually using the NUS buffer!
            if (k_mutex_lock(&nus_buffer_mutex, K_MSEC(100)) != 0) {
                printk("Mutex not unlocking!\n");
                return;
            }

            // Add to the buffer.
            strcat(nus_buffer, buffer);
            atomic_inc(&buffer_count);

            // If reached the count, send it out over NUS.
            if (atomic_get(&buffer_count) == NUS_ITEM_COUNT) {
                int err = bt_nus_send(NULL, nus_buffer, strlen(nus_buffer));
                if (err < 0 && (err != -EAGAIN) && (err != -ENOTCONN)) {
                    printk("Error sending!\n");
                }

                atomic_set(&buffer_count, 0);
                nus_buffer[0] = '\0';
            }
            k_mutex_unlock(&nus_buffer_mutex);
        }
    }
}

/* ========================================================================== */
/* NUS Services                                                               */
/* ========================================================================== */

// Callback to print when a connection has occured.
static void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    printk("Subscriber %s\n", (enabled ? "enabled" : "disabled"));
}

// Print received data (not really relevant, but here for testing purposes if
// required.
static void received(struct bt_conn *conn, const void *data, uint16_t len,
                     void *ctx)
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

struct bt_nus_cb nus_listener = {.notif_enabled = notif_enabled,
                                 .received = received};

/* ========================================================================== */
/* Connection CBs                                                             */
/* ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t err)
{
    bt_conn_ref(conn);
    printk("Connected!\n");
}

// Disconnection stops advertising - in case the base Node disconnects, do 
// make sure to restart advertising again.
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected, reason 0x%02x %s\n", reason,
           bt_hci_err_to_str(reason));

    bt_conn_unref(conn);
    printk("Cleared previous connection\n");

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), 0, 0);
    if (err) {
        printk("Failed to start advertising %d", err);
        return;
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {.connected = connected,
                                     .disconnected = disconnected};

// Display new MTU values (buffer size for NUS)
static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    printk("Updated MTU, TX: %d, RX: %d\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

/* ========================================================================== */
/* Initialisation                                                             */
/* ========================================================================== */

int main(void)
{
    int err = bt_nus_cb_register(&nus_listener, NULL);
    if (err) {
        printk("Failed to register NUS bluetooth callback");
        return err;
    }

    err = bt_enable(NULL);
    if (err) {
        printk("Failed to intiialise bluetooth");
        return err;
    }

    bt_gatt_cb_register(&gatt_callbacks);
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), 0, 0);
    if (err) {
        printk("Failed to start advertising");
        return err;
    }

    err = bt_le_scan_start(&scan_param, scan_found);
    if (err) {
        printk("Bluetooth failed to start observing!");
        return err;
    }

    return 0;
}
