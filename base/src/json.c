/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "json.h"
#include "zephyr/data/json.h"

/* ========================================================================== */
/* Command Descriptor                                                         */
/* ========================================================================== */

static const struct json_obj_descr cmd_descr[] = {
};

/* ========================================================================== */
/* Encode / Decode                                                            */
/* ========================================================================== */

/* Encodes a cmd_json struct into a JSON string in the provided buffer. */
int encode_json_cmd(struct json_packet *val, char *buffer, size_t buf_size)
{
    int ret = json_obj_encode_buf(cmd_descr, ARRAY_SIZE(cmd_descr), val, buffer, buf_size);
    if (ret < 0) {
        return (ret);
    }

    return (0);
}

/* Decodes a JSON string into a cmd_json struct. */
int decode_json_cmd(char *input, size_t len, struct json_packet *data)
{
    int ret = json_obj_parse(input, len, cmd_descr, ARRAY_SIZE(cmd_descr), data);
    if (ret < 0) {
        return (ret);
    }

    return (0);
}
