/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct helm_packet {
    uint64_t magnet_dt;
    uint8_t charge;
    int32_t mv;
    uint8_t connection_status;
};

struct json_packet {
    struct helm_packet nodeA;
    struct helm_packet nodeB;
    uint64_t speed;
    int8_t direction;
};

struct cmd_packet {
    uint8_t cmd;
    uint16_t pulse;
    time_t time;
};

int encode_cmd_packet(struct cmd_packet *val, char *buffer, size_t buf_size);
int decode_cmd_packet(char *input, size_t len, struct cmd_packet *data);
int encode_json_packet(struct json_packet *val, char *buffer, size_t buf_size);
int decode_json_packet(char *input, size_t len, struct json_packet *data);

#endif
