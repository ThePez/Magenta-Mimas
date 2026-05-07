/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <zephyr/data/json.h>

#define MAX_CMD_OPTS 26 // iBeacon add ... is longest cmd

struct cmd_json {
    char *cmd;                // "pos", "view", "ble", "iBeacon"
    char *sub;                // "add", "delete", "start", "end", "node", "stop"
    char *opts[MAX_CMD_OPTS]; // options need for sub
    size_t len;
};

struct beacon_json {
    char *cmd;   // "found"
    char *name;  // beacon name
    char *major; // and so on...
    char *minor;
    char *addr;
    char *rssi;
};

int encode_json_beacon(struct beacon_json *val, char *buffer, size_t buf_size);
int encode_json_cmd(struct cmd_json *val, char *buffer, size_t buf_size);
int decode_json_cmd(char *input, struct cmd_json *data);

#endif
