/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rb_tree.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include "zephyr/sys/printk.h"
#include <zephyr/sys/util.h>
#include <sys/errno.h>

#define NAME_LENGTH 10

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

// Mutex Lock to protect the tree from curuption
K_MUTEX_DEFINE(rbLock);

struct peripheral_data data_list[NUM_BEACONS] = {
    {0},
    {0},
};

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
    struct peripheral_data *n1 = CONTAINER_OF(a, struct peripheral_data, rbnode);
    struct peripheral_data *n2 = CONTAINER_OF(b, struct peripheral_data, rbnode);

    uint32_t key1 = ((uint32_t)n1->major << 16) | n1->minor;
    uint32_t key2 = ((uint32_t)n2->major << 16) | n2->minor;

    return (key1 < key2);
}

/* Inserts all default beacons into the tree. */
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
struct peripheral_data *get_rb_node(uint32_t target)
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
        struct peripheral_data *bd = CONTAINER_OF(node, struct peripheral_data, rbnode);
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
    struct peripheral_data *node = get_rb_node(key);
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
    struct peripheral_data *new =
        (struct peripheral_data *)k_malloc(sizeof(struct peripheral_data));
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
