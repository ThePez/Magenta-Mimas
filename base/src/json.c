/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "json.h"
#include "zephyr/data/json.h"

#define DOUBLE_SCALE 1000

struct bat_wire {
    time_t timestamp;
    int64_t bat_mv;
    int32_t bat_charge;
};

struct imu_wire {
    time_t timestamp;
    int64_t gyro_rads;
    int64_t accel_ms2;
};

struct helm_wire {
    struct imu_wire imu;
    struct bat_wire bat;
    uint8_t connection_status;
    uint8_t id;
};

struct json_wire {
    time_t time;
    struct helm_wire nodeA;
    struct helm_wire nodeB;
    uint64_t speed;
    int8_t direction;
};

/* ========================================================================== */
/* Descriptors                                                                */
/* ========================================================================== */

static const struct json_obj_descr bat_json_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct bat_wire, timestamp, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct bat_wire, bat_mv, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct bat_wire, bat_charge, JSON_TOK_INT),
};

static const struct json_obj_descr imu_json_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct imu_wire, timestamp, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct imu_wire, gyro_rads, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct imu_wire, accel_ms2, JSON_TOK_INT64),
};

static const struct json_obj_descr helm_json_descr[] = {
    JSON_OBJ_DESCR_OBJECT(struct helm_wire, imu, imu_json_descr),
    JSON_OBJ_DESCR_OBJECT(struct helm_wire, bat, bat_json_descr),
    JSON_OBJ_DESCR_PRIM(struct helm_wire, connection_status, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct helm_wire, id, JSON_TOK_UINT),
};

static const struct json_obj_descr json_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct json_wire, time, JSON_TOK_INT64),
    JSON_OBJ_DESCR_OBJECT(struct json_wire, nodeA, helm_json_descr),
    JSON_OBJ_DESCR_OBJECT(struct json_wire, nodeB, helm_json_descr),
    JSON_OBJ_DESCR_PRIM(struct json_wire, speed, JSON_TOK_INT64),
    JSON_OBJ_DESCR_PRIM(struct json_wire, direction, JSON_TOK_INT),
};

static const struct json_obj_descr cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, cmd, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, pulse, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct cmd_packet, time, JSON_TOK_INT64),
};

static inline void imu_to_wire(struct imu_wire *w, const struct imu_data *d)
{
    w->timestamp = d->timestamp;
    w->gyro_rads = (int64_t)(d->gyro_rads * DOUBLE_SCALE);
    w->accel_ms2 = (int64_t)(d->accel_ms2 * DOUBLE_SCALE);
}

static inline void wire_to_imu(const struct imu_wire *w, struct imu_data *d)
{
    d->timestamp = w->timestamp;
    d->gyro_rads = w->gyro_rads / (double)DOUBLE_SCALE;
    d->accel_ms2 = w->accel_ms2 / (double)DOUBLE_SCALE;
}

static inline void bat_to_wire(struct bat_wire *w, const struct bat_packet *b)
{
    w->timestamp = b->timestamp;
    w->bat_mv = (int64_t)(b->bat_mv * DOUBLE_SCALE);
    w->bat_charge = b->bat_charge;
}

static inline void wire_to_bat(const struct bat_wire *w, struct bat_packet *b)
{
    b->timestamp = w->timestamp;
    b->bat_mv = w->bat_mv / (double)DOUBLE_SCALE;
    b->bat_charge = w->bat_charge;
}

static inline void helm_to_wire(struct helm_wire *w, const struct helm_packet *h)
{
    imu_to_wire(&w->imu, &h->imu);
    bat_to_wire(&w->bat, &h->bat);
    w->connection_status = h->connection_status;
    w->id = h->id;
}

static inline void wire_to_helm(const struct helm_wire *w, struct helm_packet *h)
{
    wire_to_imu(&w->imu, &h->imu);
    wire_to_bat(&w->bat, &h->bat);
    h->connection_status = w->connection_status;
    h->id = w->id;
}

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
    return (ret < 0) ? (ret) : 0;
}

/* Encodes a cmd_json struct into a JSON string in the provided buffer. */
int encode_json_packet(struct json_packet *val, char *buffer, size_t buf_size)
{
    struct json_wire wire = {
        .direction = val->direction,
        .speed = val->speed * DOUBLE_SCALE,
        .time = val->time,
    };

    helm_to_wire(&wire.nodeA, &val->nodeA);
    helm_to_wire(&wire.nodeB, &val->nodeB);

    int ret = json_obj_encode_buf(json_descr, ARRAY_SIZE(json_descr), &wire, buffer, buf_size);
    return (ret);
}

/* Decodes a JSON string into a cmd_json struct. */
int decode_json_packet(char *input, size_t len, struct json_packet *data)
{
    struct json_wire wire = {0};
    int ret = json_obj_parse(input, len, json_descr, ARRAY_SIZE(json_descr), &wire);
    if (ret < 0) {
        return ret;
    }

    data->time = wire.time;
    data->speed = wire.speed / (double)DOUBLE_SCALE;
    data->direction = wire.direction;
    wire_to_helm(&wire.nodeA, &data->nodeA);
    wire_to_helm(&wire.nodeB, &data->nodeB);
    return 0;
}
