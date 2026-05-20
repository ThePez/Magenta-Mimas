/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "json.h"
#include "zephyr/data/json.h"

/* ========================================================================== */
/* Descriptors                                                                */
/* ========================================================================== */

static const struct json_obj_descr helm_json_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct helm_packet, magnet_dt, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct helm_packet, charge, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct helm_packet, mv, JSON_TOK_INT),
    JSON_OBJ_DESCR_PRIM(struct helm_packet, connection_status, JSON_TOK_UINT),
};

static const struct json_obj_descr json_descr[] = {
    JSON_OBJ_DESCR_OBJECT(struct json_packet, nodeA, helm_json_descr),
    JSON_OBJ_DESCR_OBJECT(struct json_packet, nodeB, helm_json_descr),
    JSON_OBJ_DESCR_PRIM(struct json_packet, speed, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct json_packet, direction, JSON_TOK_INT),
};

static const struct json_obj_descr cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, cmd, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, pulse, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, time, JSON_TOK_INT64),
};

/* ========================================================================== */
/* Encode / Decode                                                            */
/* ========================================================================== */

int encode_cmd_packet(struct cmd_packet *val, char *buffer, size_t buf_size)
{
    int ret = json_obj_encode_buf(cmd_descr, ARRAY_SIZE(cmd_descr), val, buffer, buf_size);
    return (ret);
}

int decode_cmd_packet(char *input, size_t len, struct cmd_packet *data)
{
    int ret = json_obj_parse(input, len, cmd_descr, ARRAY_SIZE(cmd_descr), data);
    return (ret);
}

/* Encodes a cmd_json struct into a JSON string in the provided buffer. */
int encode_json_packet(struct json_packet *val, char *buffer, size_t buf_size)
{
    int ret = json_obj_encode_buf(json_descr, ARRAY_SIZE(json_descr), val, buffer, buf_size);
    return (ret);
}

/* Decodes a JSON string into a cmd_json struct. */
int decode_json_packet(char *input, size_t len, struct json_packet *data)
{
    int ret = json_obj_parse(input, len, json_descr, ARRAY_SIZE(json_descr), data);
    return (ret);
}
