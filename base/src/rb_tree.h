/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RB_TREE_H
#define RB_TREE_H

#include <stdint.h>
#include <zephyr/sys/rb.h>

#define NUM_HELMS 2

struct helm_node {
    /* Latest control packet (helm_control_data) */
    int64_t ctrl_timestamp;
    int64_t halleffect_time;
    int16_t x;
    int16_t y;
    int16_t z;
    /* Latest status packet (helm_status_data) */
    int64_t status_timestamp;
    uint16_t mv;

    uint16_t id;      /* sort key — 0 = Helm-A, 1 = Helm-B for the two static nodes */
    uint8_t dynamic; /* 1 if heap-allocated, 0 if from helm_list[] */
    struct rbnode rbnode;
};

int rb_lock(void);
int rb_lock_no_wait(void);
int rb_unlock(void);
void init_rb_tree(void);
struct helm_node *get_rb_node(uint16_t id);
int get_rb_tree_size(void);
int remove_rb_node(uint16_t id);
int insert_rb_node(uint16_t id);
void print_rb_node(void);

extern struct helm_node helm_list[NUM_HELMS];

#endif
