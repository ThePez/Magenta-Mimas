/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rb_tree.h"
#include "common.h"

#include "gatt.h"

#include <stdio.h>
#include <string.h>
#include <sys/errno.h>

#include <zephyr/kernel.h>
#include "zephyr/sys/printk.h"

#define RB_STACK    2048
#define RB_PRIORITY 6

static bool helm_lessthan_func(struct rbnode *a, struct rbnode *b);

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

static struct rbtree tree = {.lessthan_fn = helm_lessthan_func};
static uint8_t initialised = 0;
static int size = 0;

// Tree Mutex
K_MUTEX_DEFINE(rbLock);

// Tree Initialised Semaphore
K_SEM_DEFINE(rb_semaphore, 0, 1);

// Sensor Semaphore
K_SEM_DEFINE(sensor_semaphore, 0, 1);

#define RECEIVED_MASK 0x03

struct helm_node helm_list[NUM_HELMS] = {
    [0] = {.id = 0},
    [1] = {.id = 1},
};

/* ========================================================================== */
/* Tree Protection                                                            */
/* ========================================================================== */

/**
 * @brief Attempt to acquire the rb_tree mutex, timeout after 10ms.
 * @return 0 on success, negative errno on timeout.
 */
int rb_lock(void)
{
    return k_mutex_lock(&rbLock, K_MSEC(10));
}

/**
 * @brief Attempt to acquire the rb_tree mutex without waiting.
 * @return 0 on success, -EBUSY if unavailable.
 */
int rb_lock_no_wait(void)
{
    return k_mutex_lock(&rbLock, K_NO_WAIT);
}

/**
 * @brief Release the rb_tree mutex.
 * @return 0 on success, negative errno on failure.
 */
int rb_unlock(void)
{
    return k_mutex_unlock(&rbLock);
}

/* ========================================================================== */
/* Tree Operations                                                            */
/* ========================================================================== */

static bool helm_lessthan_func(struct rbnode *a, struct rbnode *b)
{
    struct helm_node *n1 = CONTAINER_OF(a, struct helm_node, rbnode);
    struct helm_node *n2 = CONTAINER_OF(b, struct helm_node, rbnode);

    return (n1->id < n2->id);
}

/**
 * @brief Insert pre-allocated nodes for both helms and mark the tree ready.
 *        Safe to call multiple times — subsequent calls are no-ops.
 */
static void init_rb_tree(void)
{
    if (initialised) {
        return;
    }

    k_mutex_lock(&rbLock, K_FOREVER);

    for (int i = 0; i < ARRAY_SIZE(helm_list); i++) {
        rb_insert(&tree, &helm_list[i].rbnode);
        size++;
    }

    initialised = 1;
    k_mutex_unlock(&rbLock);
}

/**
 * @brief Look up a helm node by its id.
 *
 * @param id  Helm identifier to search for.
 * @return Pointer to the matching helm_node, or NULL if not found or tree
 *         uninitialised.
 */
struct helm_node *get_rb_node(uint16_t id)
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
        struct helm_node *hn = CONTAINER_OF(node, struct helm_node, rbnode);

        if (id == hn->id) {
            rb_unlock();
            return (hn);
        } else if (id < hn->id) {
            node = z_rb_child(node, 0);
        } else {
            node = z_rb_child(node, 1);
        }
    }

    rb_unlock();
    return (NULL);
}

/**
 * @brief Return the number of nodes currently in the tree.
 * @return Current tree size.
 */
int get_rb_tree_size(void)
{
    return size;
}

/* ========================================================================== */
/* Node Insert / Remove                                                       */
/* ========================================================================== */

/**
 * @brief Remove a helm node from the tree by id. Frees heap-allocated nodes.
 *
 * @param id  Helm identifier to remove.
 * @return 0 on success, -EBUSY if the lock is unavailable, -ESRCH if not found.
 */
int remove_rb_node(uint16_t id)
{
    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        return (-EBUSY);
    }

    struct helm_node *node = get_rb_node(id);
    if (node == NULL) {
        rb_unlock();
        return (-ESRCH);
    }

    rb_remove(&tree, &node->rbnode);
    size--;

    if (node->dynamic) {
        k_free(node);
    }

    rb_unlock();
    return (0);
}

/**
 * @brief Insert a node with the given id into the tree.
 *
 * For ids 0 and 1, re-inserts the corresponding pre-allocated helm_list entry.
 * For any other id, allocates a new node on the heap. Returns -EINVAL if a
 * node with that id already exists.
 *
 * @param id  Helm identifier for the new node.
 * @return 0 on success, -EBUSY if the lock is unavailable, -EINVAL if the id
 *         is already present, -ENOMEM if heap allocation fails.
 */
int insert_rb_node(uint16_t id)
{
    if (rb_lock()) {
        printk("[ERROR] rbTree Mutex unavailable\n");
        return (-EBUSY);
    }

    if (get_rb_node(id) != NULL) {
        rb_unlock();
        return (-EINVAL);
    }

    /* Re-insert one of the two pre-allocated static nodes */
    if (id < NUM_HELMS) {
        rb_insert(&tree, &helm_list[id].rbnode);
        size++;
        rb_unlock();
        return (0);
    }

    /* Arbitrary new node — heap allocate */
    struct helm_node *new = (struct helm_node *)k_malloc(sizeof(struct helm_node));
    if (new == NULL) {
        rb_unlock();
        return (-ENOMEM);
    }

    memset(new, 0, sizeof(*new));
    new->id = id;
    new->dynamic = 1;

    rb_insert(&tree, &new->rbnode);
    size++;

    rb_unlock();
    return (0);
}

/* ========================================================================== */
/* Red Black Tree Thread                                                      */
/* ========================================================================== */

void thread_rb(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    init_rb_tree();
    // Notify main() that the tree is setup
    k_sem_give(&rb_semaphore);

    struct helm_node *node;
    struct ble_packet packet;

    while (1) {
        // uint8_t received_mask = 0;
        // Drain all packets from the queue until a sensor packet from both nodes has been received
        // while (received_mask != RECEIVED_MASK) {
        if (k_msgq_get(&gatt_msg_queue, &packet, K_FOREVER) == 0) {
            rb_lock();
            node = get_rb_node(packet.node_num);
            if (node != NULL) {
                switch (packet.packet_id) {
                case SENSOR:
                    node->imu_data = packet.data.imu;
                    // received_mask |= BIT(packet.node_num);
                    k_sem_give(&sensor_semaphore);
                    break;
                case BATTERY:
                    node->battery_data = packet.data.bat;
                    break;
                }
            }

            rb_unlock();
        }
        // }

        // Once a sensor packet from both nodes has been received -> give semaphore
        // k_sem_give(&sensor_semaphore);
    }
}

K_THREAD_DEFINE(rb_thread, RB_STACK, thread_rb, NULL, NULL, NULL, RB_PRIORITY, 0, 0);
