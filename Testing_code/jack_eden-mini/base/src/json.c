/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "json.h"
#include "zephyr/data/json.h"
#include <sys/errno.h>

/* ========================================================================== */
/* Command Descriptor                                                         */
/* Describes the cmd_json struct layout for the Zephyr JSON codec.            */
/* Used for both encoding outgoing commands and decoding incoming ones.       */
/*                                                                            */
/* Wire format:                                                               */
/*   {"cmd":"<cmd>","sub":"<sub>","opts":["<opt0>","<opt1>", ...]}            */
/* ========================================================================== */

static const struct json_obj_descr cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct cmd_json, cmd, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct cmd_json, sub, JSON_TOK_STRING),
    JSON_OBJ_DESCR_ARRAY(struct cmd_json, opts, MAX_CMD_OPTS, len, JSON_TOK_STRING),
};

/* ========================================================================== */
/* iBeacon Descriptor                                                         */
/* Describes the beacon_json struct layout for the Zephyr JSON codec.         */
/* Used only for encoding outgoing iBeacon packets in LISTEN mode.            */
/*                                                                            */
/* Wire format:                                                               */
/*   {"cmd":"found","name":"<n>","major":"0xXXXX","minor":"0xXXXX",         */
/*    "addr":"<addr>","rssi":"<rssi>"}                                        */
/* ========================================================================== */

static const struct json_obj_descr beacon_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct beacon_json, cmd, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct beacon_json, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct beacon_json, major, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct beacon_json, minor, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct beacon_json, addr, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct beacon_json, rssi, JSON_TOK_STRING)};

/* ========================================================================== */
/* Encode / Decode                                                            */
/* ========================================================================== */

/* Encodes a beacon_json struct into a JSON string in the provided buffer. */
int encode_json_beacon(struct beacon_json *val, char *buffer, size_t buf_size)
{
    // buffer needs to be big enough to house the json string
    int ret = json_obj_encode_buf(beacon_descr, ARRAY_SIZE(beacon_descr), val, buffer, buf_size);
    if (ret < 0) {
        return (ret);
    }

    return (0);
}

/* Encodes a cmd_json struct into a JSON string in the provided buffer. */
int encode_json_cmd(struct cmd_json *val, char *buffer, size_t buf_size)
{
    // examples:
    // {"cmd":"pos","sub":"raw","opts":["x", "y", "time"]}
    // {"cmd":"pos","sub":"kalman","opts":["x","y", "time"]}
    // buffer needs to be big enough to house the json string
    int ret = json_obj_encode_buf(cmd_descr, ARRAY_SIZE(cmd_descr), val, buffer, buf_size);
    if (ret < 0) {
        return (ret);
    }

    return (0);
}

/* Decodes a JSON string into a cmd_json struct. */
int decode_json_cmd(char *input, struct cmd_json *data)
{
    /* The input string is modified in place by the Zephyr JSON parser. */
    /* Pointers in the output struct point into the input buffer.       */
    /* Do not free or overwrite input while the struct is still in use. */

    // examples:
    // {"cmd":"iBeacon","sub":"delete","opts":["<name>"]}
    // {"cmd":"iBeacon","sub":"add","opts":["<name>", ...]}
    // {"cmd":"iBeacon","sub":"view","opts":["-a"]}
    // {"cmd":"iBeacon","sub":"view","opts":["4011-B"]}
    // {"cmd":"ble","sub":"start","opts":[]}
    // {"cmd":"ble","sub":"stop","opts":[]}
    int ret = json_obj_parse(input, strlen(input), cmd_descr, ARRAY_SIZE(cmd_descr), data);
    if (ret < 0) {
        return (ret);
    }

    return (0);
}
