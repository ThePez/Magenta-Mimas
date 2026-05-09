/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JSON_H
#define JSON_H

#include <stddef.h>

struct json_packet {
};

int encode_json_cmd(struct json_packet *val, char *buffer, size_t buf_size);
int decode_json_cmd(char *input, size_t len, struct json_packet *data);

#endif
