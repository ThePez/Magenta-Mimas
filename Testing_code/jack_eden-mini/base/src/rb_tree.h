/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RB_TREE_H
#define RB_TREE_H

#include <stdint.h>
#include <zephyr/sys/rb.h>

#define RSSI_RING_BUFFER_SIZE     10
#define DISTANCE_RING_BUFFER_SIZE 10
#define NUM_BEACONS               13

// Ring Buffer to store RSSI readings
struct rssi_ring {
    int64_t timestamp[RSSI_RING_BUFFER_SIZE];
    int8_t rssi[RSSI_RING_BUFFER_SIZE];
    uint8_t head;
    uint8_t count;
};

struct distance_ring {
    int64_t timestamp[DISTANCE_RING_BUFFER_SIZE];
    double distance[DISTANCE_RING_BUFFER_SIZE];
    int8_t rssi[DISTANCE_RING_BUFFER_SIZE];
    uint8_t head;
    uint8_t count;
};

// red/black tree data structure
struct beacon_data {
    double weight;
    uint16_t x_corr;
    uint16_t y_corr;
    char *name;
    char *left_name;
    char *right_name;
    uint16_t major;
    uint16_t minor;
    uint8_t dynamic;
    int8_t cali;
    uint8_t mac[6];
    struct rssi_ring rssi_buffer;
    struct distance_ring distance_buffer;
    struct rbnode rbnode;
};

int rb_lock(void);
int rb_lock_no_wait(void);
int rb_unlock(void);
void init_rb_tree(void);
struct beacon_data *get_rb_node(uint32_t target);
int get_rb_tree_size(void);
void print_rb_node(char *name, bool all);
int remove_rb_node(char *name, uint16_t major, uint16_t minor);
int insert_rb_node(char *name, uint8_t mac[6], uint16_t major, uint16_t minor, double x, double y,
                   int8_t cali, char *left, char *right);

extern struct beacon_data data_list[NUM_BEACONS];

#endif
