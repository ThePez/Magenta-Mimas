/* ============================================================== */
/* Sniffer Mode Source                                            */
/* Written: Muhammed A                                            */
/* ============================================================== */

#include "sniffer.h"

#include <stdio.h>
#include <strings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>

#define HEAP_SIZE          1024
#define IBEACON_ID_LEN     4
#define JSON_BUFFER_SIZE   128
#define MAX_NAME_LENGTH    30
#define ADDRESS_BUFFER_LEN 18

/* ========================================================================== */
/* Constant Decs                                                              */
/* ========================================================================== */

// Define a heap to store our JSON in - we don't want to put big buffers on our
// stack
K_HEAP_DEFINE(json_buffer_heap, HEAP_SIZE);

struct iBeacon_package {
    char *address;
    int major;
    int minor;
    int rssi;
    bool is_iBeacon;
};

static const struct json_obj_descr iBeacon_package_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct iBeacon_package, address, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_package, major, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_package, minor, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_package, rssi, JSON_TOK_NUMBER)};

// Filtering iBeacons..
static const char iBeaconId[IBEACON_ID_LEN] = {0x4c, 0x00, 0x02, 0x15};

atomic_t sniffer_active = ATOMIC_INIT(false);

/* ========================================================================== */
/* Beacon Detection                                                           */
/* ========================================================================== */

// set the user_data iBeacon found to true if it is one, otherwise just ignore.
static bool mfg_data_cb(struct bt_data *data, void *user_data)
{
    struct iBeacon_package *pkg = user_data;

    switch (data->type) {
    case BT_DATA_MANUFACTURER_DATA:
        uint16_t len = data->data_len;
        if (len > 5) {
            for (int i = 0; i < IBEACON_ID_LEN; i++) {
                if (iBeaconId[i] != data->data[i]) {
                    return false;
                }
            }

            pkg->is_iBeacon = true;
            pkg->major = (data->data[len - 5] << 8) | (data->data[len - 4]);
            pkg->minor = (data->data[len - 3] << 8) | (data->data[len - 2]);
        }
        return false;
    default:
        return true;
    }
}

// This is called from nus.c - it returns true if addr was run through
// bt_data_parse (i.e. if its data was destroyed) and false otherwise - this
// includes if the item was not actually an iBeacon.
bool sniffer_write(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                   struct net_buf_simple *ad)
{
    if (ad->len >= 1 && atomic_get(&sniffer_active)) {
        struct iBeacon_package pkg = {.is_iBeacon = false, .rssi = rssi};
        bt_data_parse(ad, mfg_data_cb, &pkg);

        if (pkg.is_iBeacon) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, BT_ADDR_LE_STR_LEN);
            addr_str[ADDRESS_BUFFER_LEN - 1] = 0;

            pkg.address = addr_str;

            char *json_buffer =
                k_heap_alloc(&json_buffer_heap, JSON_BUFFER_SIZE, K_NO_WAIT);

            // Make sure to check so we don't run into a segfault!
            if (json_buffer == NULL) {
                printk("L: Run out of JSON memory!\n");
                return true;
            }

            // And then just encode the iBeacon as a JSON string.
            if (json_obj_encode_buf(iBeacon_package_descr,
                                    ARRAY_SIZE(iBeacon_package_descr), &pkg,
                                    json_buffer, JSON_BUFFER_SIZE) == 0) {
                printk("S: %s\n", json_buffer);
                k_msleep(1);
            }

            k_heap_free(&json_buffer_heap, json_buffer);
        }

        return true;
    }

    return false;
}
