/* ============================================================== */
/* Beacons data header                                            */
/* Written: Muhammed A                                            */
/* ============================================================== */

#ifndef BASE_BEACONS_H
#define BASE_BEACONS_H

#include "nus.h"

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>

#define MAX_ARB_COUNT           20
#define MAX_BEACON_COUNT        13
#define MAX_NAME_LEN            30
#define MULTILATERATION_BEACONS 4
#define NAME_INITIAL_BUFFER_LEN 7

#define BEACONS_RSSI_A (-57)
#define BEACONS_RSSI_N 3.2

/* ========================================================================== */
/* iBeacon structures                                                         */
/* ========================================================================== */

// Basically iBeacon_data is used for the beacons that require multilateration.
// iBeacon_info is used for any sort of beacon that is added to the device, and
// an iBeacon_data requires a reference to an existing iBeacon_info node.

struct iBeacon_info {
    sys_snode_t node;

    char *name;
    char *address;

    // Have to be int because of JSON, sadly
    int major;
    int minor;
    int x;
    int y;

    char *left_name; // These can be empty ("") but not NULL
    char *right_name;
};

struct iBeacon_data {
    sys_snode_t node;
    // Just simpler this way when calculating the location...
    struct iBeacon_info *info;
    double distance;
};

static const struct json_obj_descr iBeacon_info_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, address, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, major, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, minor, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, x, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, y, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, left_name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct iBeacon_info, right_name, JSON_TOK_STRING),
};

/* ========================================================================== */
/* Initial settings                                                           */
/* ========================================================================== */

static const int16_t xs_init[MAX_BEACON_COUNT] = {
    170, 0, -150, -150, -150, -150, -150, 0, 170, 170, 170, 170, 0};

static const int16_t ys_init[MAX_BEACON_COUNT] = {
    -450, -450, -450, -200, 0, 225, 400, 400, 400, 200, 0, -250, 0};

static const uint16_t majors_init[MAX_BEACON_COUNT] = {
    2753,  32975, 26679, 41747, 30679, 6195, 30525,
    57395, 60345, 12249, 36748, 27564, 49247};

static const uint16_t minors_init[MAX_BEACON_COUNT] = {
    32998, 20959, 40363, 38800, 51963, 18394, 30544,
    28931, 49995, 30916, 11457, 27589, 52925};

static const char name_init[NAME_INITIAL_BUFFER_LEN] = "4011-A";

static const char addresses_init[MAX_BEACON_COUNT][ADDRESS_BUFFER_LEN] = {
    "F5:75:FE:85:34:67", "E5:73:87:06:1E:86", "CA:99:9E:FD:98:B1",
    "CB:1B:89:82:FF:FE", "D4:D2:A0:A4:5C:AC", "C1:13:27:E9:B7:7C",
    "F1:04:48:06:39:A0", "CA:0C:E0:DB:CE:60", "D4:7F:D4:7C:20:13",
    "F7:0B:21:F1:C8:E1", "FD:E0:8D:FA:3E:4A", "EE:32:F7:28:FA:AC",
    "F7:3B:46:A8:D7:2C"};

/* ========================================================================== */
/* State definitions                                                          */
/* ========================================================================== */

extern struct k_mutex beacons_mutex;
extern sys_slist_t slist_iBeacon;

/* ========================================================================== */
/* Function definitions                                                       */
/* ========================================================================== */

int initialise_beacons();

int add_beacon(char name);
int add_arbitrary_beacon(struct iBeacon_info *info_args);
int remove_beacon(char name);
int remove_arbitrary_beacon(char *address);
int list_beacons();

#endif
