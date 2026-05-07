/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rb_tree.h"
#include "uart.h"
#include "json.h"
#include "zephyr/sys/printk.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <sys/errno.h>

#ifndef UART_USB_C
#include <zephyr/sys/printk.h>
#endif

#define NAME_LENGTH  10
#define JSON_BUF_LEN 256

static bool beacon_data_lessthan_func(struct rbnode *a, struct rbnode *b);

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

// Root of the red black tree that stores the data related to all nodes
static struct rbtree tree = {.lessthan_fn = beacon_data_lessthan_func};

// Is the red black tree initialised
static uint8_t initialised = 0;

// Size of the red black tree
static int size = NUM_BEACONS;

// Buffer used for print_node function
static char json_buf[JSON_BUF_LEN];

// Mutex Lock to protect the tree from curuption
K_MUTEX_DEFINE(rbLock);

/* ========================================================================== */
/* Default iBeacon Data                                                       */
/* The 13 known lab beacons, pre-populated with MAC, major/minor keys,        */
/* calibrated RSSI at 1m, physical coordinates in mm, and neighbour names     */
/* for the logical tree structure used by the GUI.                            */
/* These nodes are statically allocated - dynamic flag is 0.                  */
/* ========================================================================== */

/* clang-format off */
// Default known iBeacons 
struct beacon_data data_list[NUM_BEACONS] = {
    {.name = "4011-A",
        .mac = {0xF5, 0x75, 0xFE, 0x85, 0x34, 0x67},
        .major = 0x0AC1, .minor = 0x80E6, .dynamic = 0, .cali = -57,
        .x_corr = 3400, .y_corr = 0,
        .left_name = "NULL", .right_name = "4011-L"
    },
    {.name = "4011-B",
        .mac = {0xE5, 0x73, 0x87, 0x06, 0x1E, 0x86},
        .major = 0x80CF, .minor = 0x51DF, .dynamic = 0, .cali = -61,
        .x_corr = 1700, .y_corr = 0,
        .left_name = "NULL", .right_name = "4011-M"
    },
    {.name = "4011-C",
        .mac = {0xCA, 0x99, 0x9E, 0xFD, 0x98, 0xB1},
        .major = 0x6837, .minor = 0x9DAB, .dynamic = 0, .cali = -66,
        .x_corr = 0, .y_corr = 0,
        .left_name = "NULL", .right_name = "4011-D"
    },
    {.name = "4011-D",
        .mac = {0xCB, 0x1B, 0x89, 0x82, 0xFF, 0xFE},
        .major = 0xA313, .minor = 0x9790, .dynamic = 0, .cali = -62,
        .x_corr = 0, .y_corr = 2000,
        .left_name = "4011-C", .right_name = "4011-E"
    },
    {.name = "4011-E",
        .mac = {0xD4, 0xD2, 0xA0, 0xA4, 0x5C, 0xAC},
        .major = 0x77D7, .minor = 0xCAFB, .dynamic = 0, .cali = -70,
        .x_corr = 0, .y_corr = 4500,
        .left_name = "4011-D", .right_name = "4011-F"
    },
    {.name = "4011-F",
        .mac = {0xC1, 0x13, 0x27, 0xE9, 0xB7, 0x7C},
        .major = 0x1833, .minor = 0x47DA, .dynamic = 0, .cali = -60,
        .x_corr = 0, .y_corr = 6750,
        .left_name = "4011-E", .right_name = "4011-G"
    },
    {.name = "4011-G",
        .mac = {0xF1, 0x04, 0x48, 0x06, 0x39, 0xA0},
        .major = 0x773D, .minor = 0x7750, .dynamic = 0, .cali = -61,
        .x_corr = 0, .y_corr = 8500,
        .left_name = "4011-F", .right_name = "NULL"
    },
    {.name = "4011-H",
        .mac = {0xCA, 0x0C, 0xE0, 0xDB, 0xCE, 0x60},
        .major = 0xE033, .minor = 0x7103, .dynamic = 0, .cali = -60,
        .x_corr = 1700, .y_corr = 8500,
        .left_name = "4011-M", .right_name = "NULL"
    },
    {.name = "4011-I",
        .mac = {0xD4, 0x7F, 0xD4, 0x7C, 0x20, 0x13},
        .major = 0xEBB9, .minor = 0xC34B, .dynamic = 0, .cali = -63,
        .x_corr = 3400, .y_corr = 8500,
        .left_name = "4011-J", .right_name = "NULL"
    },
    {.name = "4011-J",
        .mac = {0xF7, 0x0B, 0x21, 0xF1, 0xC8, 0xE1},
        .major = 0x2FD9, .minor = 0x78C4, .dynamic = 0, .cali = -59,
        .x_corr = 3400, .y_corr = 6750,
        .left_name = "4011-K", .right_name = "4011-I"
    },
    {.name = "4011-K",
        .mac = {0xFD, 0xE0, 0x8D, 0xFA, 0x3E, 0x4A},
        .major = 0x8F8C, .minor = 0x2CC1, .dynamic = 0, .cali = -62,
        .x_corr = 3400, .y_corr = 4500,
        .left_name = "4011-L", .right_name = "4011-J"
    },
    {.name = "4011-L",
        .mac = {0xEE, 0x32, 0xF7, 0x28, 0xFA, 0xAC},
        .major = 0x6BAC, .minor = 0x6BC5, .dynamic = 0, .cali = -61,
        .x_corr = 3400, .y_corr = 2000,
        .left_name = "4011-A", .right_name = "4011-K"
    },
    {.name = "4011-M",
        .mac = {0xF7, 0x3B, 0x46, 0xA8, 0xD7, 0x2C},
        .major = 0xC05F, .minor = 0xCEBD, .dynamic = 0, .cali = -60,
        .x_corr = 1600, .y_corr = 4500,
        .left_name = "4011-B", .right_name = "4011-H"
    },
};
/* clang-format on */

/* ========================================================================== */
/* Tree Protection                                                            */
/* ========================================================================== */

/* Attempt to aquire the rbLock, timeout after 10ms */
int rb_lock(void)
{
    return k_mutex_lock(&rbLock, K_MSEC(10));
}

/* Attempt to aquire the rbLock, no waiting */
int rb_lock_no_wait(void)
{
    return k_mutex_lock(&rbLock, K_NO_WAIT);
}

/* Release the previously aquired rbLock, return as per k_mutex_unlock() */
int rb_unlock(void)
{
    return k_mutex_unlock(&rbLock);
}

/* ========================================================================== */
/* Tree Operations                                                            */
/* ========================================================================== */

/* Ordering function used by the Zephyr rbtree. */
static bool beacon_data_lessthan_func(struct rbnode *a, struct rbnode *b)
{
    struct beacon_data *n1 = CONTAINER_OF(a, struct beacon_data, rbnode);
    struct beacon_data *n2 = CONTAINER_OF(b, struct beacon_data, rbnode);

    uint32_t key1 = ((uint32_t)n1->major << 16) | n1->minor;
    uint32_t key2 = ((uint32_t)n2->major << 16) | n2->minor;

    return (key1 < key2);
}

/* Inserts all 13 default beacons into the tree. */
void init_rb_tree(void)
{
    if (initialised) {
        return;
    }

    // Wait forever until lock is free, realistically this will never wait.
    k_mutex_lock(&rbLock, K_FOREVER);

    for (int i = 0; i < ARRAY_SIZE(data_list); i++) {
        rb_insert(&tree, &(data_list[i].rbnode));
    }

    initialised = 1;
    // Unlock the tree
    k_mutex_unlock(&rbLock);
}

/* Searches the tree for a node matching the (major << 16 | minor) key. */
struct beacon_data *get_rb_node(uint32_t target)
{
    if (!initialised) {
        return (NULL);
    }

    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        return (NULL);
    }

    struct rbnode *node = tree.root;
    while (node != NULL) {
        struct beacon_data *bd = CONTAINER_OF(node, struct beacon_data, rbnode);
        uint32_t key = ((uint32_t)bd->major << 16) | bd->minor;

        if (target == key) {
            rb_unlock();
            return (bd);
        } else if (target < key) {
            node = z_rb_child(node, 0);
        } else {
            node = z_rb_child(node, 1);
        }
    }

    // Unlock the tree
    rb_unlock();
    return (NULL);
}

/* Prints one node by name, or all nodes if all == true. */
void print_rb_node(char *name, bool all)
{

    /* Start of json list */
    memset(json_buf, 0, sizeof(json_buf)); // 256 len
    struct cmd_json val = {.cmd = "view", .sub = "start"};
    encode_json_cmd(&val, json_buf, sizeof(json_buf));
#ifdef UART_USB_C
    print_uart(json_buf);
    print_uart("\n");
#else
    printk("%s\n", json_buf);
#endif

    // Lock the tree
    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        goto unlock;
    }

    struct beacon_data *node;
    /* Send one packet per node */
    RB_FOR_EACH_CONTAINER(&tree, node, rbnode) {
        if (all || (name && !strcmp(name, node->name))) {
            /* Small string buffers for each field */
            char mac_str[18];
            char major_str[7];
            char minor_str[7];
            char x_str[6];
            char y_str[6];
            char cali_str[5];

            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", node->mac[0],
                     node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
            snprintf(major_str, sizeof(major_str), "0x%x", node->major);
            snprintf(minor_str, sizeof(minor_str), "0x%x", node->minor);
            snprintf(x_str, sizeof(x_str), "%d", node->x_corr);
            snprintf(y_str, sizeof(y_str), "%d", node->y_corr);
            snprintf(cali_str, sizeof(cali_str), "%d", node->cali);
            struct cmd_json node_val = {.cmd = "view",
                                        .sub = "node",
                                        .opts = {node->name, mac_str, major_str, minor_str, x_str,
                                                 y_str, cali_str, node->left_name,
                                                 node->right_name},
                                        .len = 9};

            memset(json_buf, 0, sizeof(json_buf));
            encode_json_cmd(&node_val, json_buf, sizeof(json_buf));
#ifdef UART_USB_C
            print_uart(json_buf);
            print_uart("\n");
#else
            printk("%s\n", json_buf);
#endif
        }
    }

unlock:
    // Unlock the tree
    rb_unlock();

    /* End of json list */
    memset(json_buf, 0, sizeof(json_buf));
    val.sub = "end";
    encode_json_cmd(&val, json_buf, sizeof(json_buf));
#ifdef UART_USB_C
    print_uart(json_buf);
    print_uart("\n");
#else
    printk("%s\n", json_buf);
#endif
}

/* ========================================================================== */
/* Node Insert / Remove                                                       */
/* ========================================================================== */

/* Removes a node from the tree by name and (major, minor) key. */
int remove_rb_node(char *name, uint16_t major, uint16_t minor)
{
    // Lock the tree
    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        return (-EBUSY);
    }

    uint32_t key = ((uint32_t)major << 16) | minor;
    struct beacon_data *node = get_rb_node(key);
    if (node == NULL) {
        rb_unlock();
        return (-ESRCH);
    }

    struct rbnode *n = &node->rbnode;
    rb_remove(&tree, n);
    if (node->dynamic) {
        k_free(node->name);
        k_free(node->left_name);
        k_free(node->right_name);
        k_free(node);
    }

    size--;
    // Unlock the tree
    rb_unlock();
    return (0);
}

/* Inserts a new beacon node into the tree. */
int insert_rb_node(char *name, uint8_t mac[6], uint16_t major, uint16_t minor, double x, double y,
                   int8_t cali, char *left, char *right)
{
    // Lock the tree
    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        return (-EBUSY);
    }

    // Check for node already exists
    if (get_rb_node(((uint32_t)major << 16) | minor) != NULL) {
        rb_unlock();
        return (-EINVAL);
    }

    // Check if node being added is one of the 13 originals
    int8_t original = -1;
    for (uint8_t i = 0; i < NUM_BEACONS; i++) {
        if ((major == data_list[i].major) && (minor == data_list[i].minor)) {
            original = i;
            break;
        }
    }

    // Don't make a dynamic node if you're re-adding 1 of the 13
    if (original >= 0) {
        rb_insert(&tree, &(data_list[original]).rbnode);
        size++;
        // Unlock the tree
        rb_unlock();
        return (0);
    }

    // Completely new node
    struct beacon_data *new = (struct beacon_data *)k_malloc(sizeof(struct beacon_data));
    if (new == NULL) {
        goto unlock;
    }

    new->name = k_malloc(NAME_LENGTH);
    if (new->name == NULL) {
        goto free_name;
    }

    new->left_name = k_malloc(NAME_LENGTH);
    if (new->left_name == NULL) {
        goto free_left;
    }

    new->right_name = k_malloc(NAME_LENGTH);
    if (new->right_name == NULL) {
        goto free_right;
    }

    strncpy(new->name, name, NAME_LENGTH - 1);
    new->name[NAME_LENGTH - 1] = '\0';
    strncpy(new->left_name, left, NAME_LENGTH - 1);
    new->left_name[NAME_LENGTH - 1] = '\0';
    strncpy(new->right_name, right, NAME_LENGTH - 1);
    new->right_name[NAME_LENGTH - 1] = '\0';
    new->major = major;
    new->minor = minor;
    new->x_corr = x;
    new->y_corr = y;
    new->dynamic = 1;
    new->cali = cali;
    for (int i = 0; i < 6; i++) {
        new->mac[i] = mac[i];
    }

    rb_insert(&tree, &new->rbnode);
    size++;

    // Unlock the tree
    rb_unlock();
    return (0);

free_right:
    k_free(new->right_name);
free_left:
    k_free(new->left_name);
free_name:
    k_free(new->name);
unlock:
    // Unlock the tree
    rb_unlock();
    return (-ENOMEM);
}
