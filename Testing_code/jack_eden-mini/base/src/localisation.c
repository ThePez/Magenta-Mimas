/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "localisation.h"
#include "matrix.h"
#include "ble_shell.h"
#include "rb_tree.h"
#include "observer.h"
#include "base_gatt.h"

#include "zephyr/kernel.h"
#include "zephyr/sys/printk.h"
#include "zephyr/toolchain.h"
#include <stdint.h>
#include <math.h>
#include <sys/errno.h>

K_MSGQ_DEFINE(localisation_msg_queue, sizeof(struct position_data), 5, 4);

/* ========================================================================== */
/* Distance Estimation                                                        */
/* Converts an RSSI reading into an approximate distance using the log-       */
/* distance path loss model. cali_rssi is the known RSSI at 1m.              */
/* ========================================================================== */

static double estimate_distance(int8_t rssiMeasurement, int8_t rssiReference)
{
    double n = 2.5; // 2 usually good for open air
    return pow(10, (rssiReference - rssiMeasurement) / (10 * n));
}

/* ========================================================================== */
/* Multilateration                                                            */
/* Calculates the mobile node's (x, y) position in millimetres using a       */
/* least-squares solution over N >= 3 beacon distance estimates.              */
/*                                                                            */
/* The beacon with the strongest signal must be passed in readings[0] as the  */
/* reference node. The system of circle equations is linearised by            */
/* subtracting the reference equation from each other, yielding:             */
/*   A * [x, y]^T = b                                                        */
/* which is solved via the normal equations:                                  */
/*   [x, y]^T = (A^T * A)^-1 * A^T * b                                      */
/* ========================================================================== */

static int multilateration(double *dist, uint32_t *ids, int16_t *bx, int16_t *by, int8_t *cali,
                           uint8_t count, double *out_x, double *out_y)
{
    if (count < 3) {
        return (-EINVAL);
    }

    double weights[NUM_BEACONS];
    for (int i = 0; i < count; i++) {
        double d = dist[i] < 100.0 ? 100.0 : dist[i]; // clamp to 100mm minimum
        weights[i] = 1.0 / d;
        struct beacon_data *node = get_rb_node(ids[i]);
        if (node != NULL) {
            // Formated such that the GUI can find this.
            printk("[DISTANCE]||%s||%f\n", node->name, dist[i]);
        }
    }

    int rows = count - 1;
    double A[NUM_BEACONS - 1][2];
    double b[NUM_BEACONS - 1];

    // Build matrix and vector (reference was in readings[0])
    for (int i = 0; i < rows; i++) {
        A[i][0] = 2.0 * (bx[i + 1] - bx[0]);
        A[i][1] = 2.0 * (by[i + 1] - by[0]);
        b[i] = (dist[0] * dist[0]) - (dist[i + 1] * dist[i + 1]) + (bx[i + 1] * bx[i + 1]) -
               (bx[0] * bx[0]) + (by[i + 1] * by[i + 1]) - (by[0] * by[0]);
    }

    // Compute A^T * W * A  (2x2)
    double ATA[2][2] = {0};
    // Compute A^T * W * b  (2x1)
    double ATb[2] = {0};
    // Weighted Least Squares -> using the 1/d to dynamically scale each equation
    for (int i = 0; i < rows; i++) {
        // Row i uses beacon 0 and beacon i+1, so combine their weights.
        double scale = weights[0] * weights[i + 1];
        // scale = 1;
        ATA[0][0] += scale * A[i][0] * A[i][0];
        ATA[0][1] += scale * A[i][0] * A[i][1];
        ATA[1][0] += scale * A[i][1] * A[i][0];
        ATA[1][1] += scale * A[i][1] * A[i][1];
        ATb[0] += scale * A[i][0] * b[i];
        ATb[1] += scale * A[i][1] * b[i];
    }

    double inv[2][2];
    if (matrix2x2_inv(ATA, inv)) {
        // Singular matrix
        printk("[ERROR] Singular Matrix found\n");
        return (-EINVAL);
    }

    // Final result: (A^T*A)^-1 * (A^T * b)
    *out_x = inv[0][0] * ATb[0] + inv[0][1] * ATb[1];
    *out_y = inv[1][0] * ATb[0] + inv[1][1] * ATb[1];
    return (0);
}

/* ========================================================================== */
/* RSSI Ring Buffer                                                           */
/* Adds a new RSSI sample from an iBeacon packet into the node's ring         */
/* buffer, overwriting the oldest entry once the buffer is full.              */
/* ========================================================================== */

void add_beacon_sample(struct beacon_data *node, struct iBeacon *data)
{
    int64_t current = k_uptime_get();
    struct rssi_ring *rssi_buf = &node->rssi_buffer;
    rssi_buf->rssi[rssi_buf->head] = data->rssi;
    rssi_buf->timestamp[rssi_buf->head] = current;

    rssi_buf->head = (rssi_buf->head + 1) % RSSI_RING_BUFFER_SIZE;
    if (rssi_buf->count < RSSI_RING_BUFFER_SIZE) {
        rssi_buf->count++;
    }

    if (rssi_buf->count == 0) {
        return;
    }

    int sum = 0;
    int fresh = 0;

    for (int j = 0; j < rssi_buf->count; j++) {
        if ((current - rssi_buf->timestamp[j]) <= 1000) {
            sum += rssi_buf->rssi[j];
            fresh++;
        }
    }

    if (fresh == 0) {
        return;
    }

    struct distance_ring *dis_buf = &node->distance_buffer;
    dis_buf->rssi[dis_buf->head] = (int8_t)((double)sum / fresh);
    dis_buf->distance[dis_buf->head] =
        estimate_distance(dis_buf->rssi[dis_buf->head], node->cali) * 1000;
    dis_buf->timestamp[dis_buf->head] = current;

    dis_buf->head = (dis_buf->head + 1) % DISTANCE_RING_BUFFER_SIZE;
    if (dis_buf->count < DISTANCE_RING_BUFFER_SIZE) {
        dis_buf->count++;
    }
}

/* ========================================================================== */
/* Localisation Thread                                                        */
/* Runs continuously at two rates:                                            */
/*   - Fast (10ms):  drains the ibeacon_msg_queue and stores new RSSI         */
/*                   samples into each beacon's ring buffer.                  */
/*   - Slow (500ms): averages recent RSSI samples per beacon, selects the     */
/*                   strongest as the reference, and calls multilateration    */
/*                   to produce an (x, y) position estimate in millimetres.   */
/*                   Results are pushed to localisation_msg_queue.            */
/* ========================================================================== */

void thread_localisation(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    init_rb_tree();
    int64_t past = 0;
    struct iBeacon beacon = {0};
    struct position_data pos_data = {0};

    while (1) {
        // Thread is irrelivant in LISTEN MODE
        if (atomic_get(&mode) == LISTEN_MODE) {
            k_msleep(100);
            continue;
        }

        /* ------------------------------------------------------------------ */
        /* Fast loop: ingest new beacon packets into ring buffers             */
        /* ------------------------------------------------------------------ */
        if (k_msgq_get(&ibeacon_msg_queue, &beacon, K_NO_WAIT) == 0) {
            uint32_t key = ((uint32_t)beacon.major << 16) | beacon.minor;
            if (rb_lock_no_wait() == 0) {
                // Tree is locked -> can now add to buffers
                struct beacon_data *node = get_rb_node(key);
                if (node != NULL) {
                    add_beacon_sample(node, &beacon);
                }

                // Unlock when done
                rb_unlock();
            }
        }

        k_msleep(10);
        int64_t current = k_uptime_get();
        if ((current - past) < 500) {
            continue;
        }

        past = current;

        /* ------------------------------------------------------------------ */
        /* Slow loop: average RSSI per beacon and build inputs for            */
        /* multilateration. Only include beacons with fresh data (<500ms).    */
        /* ------------------------------------------------------------------ */
        double out_x, out_y;
        double distances[NUM_BEACONS] = {0};
        int8_t readings[NUM_BEACONS] = {0};
        uint32_t ids[NUM_BEACONS] = {0};
        int16_t bx[NUM_BEACONS] = {0};
        int16_t by[NUM_BEACONS] = {0};
        int8_t cali[NUM_BEACONS] = {0};
        int index = 0;

        if (rb_lock() < 0) {
            printk("[ERROR] rb mutex unavailable\n");
            // skip rest of loop
            continue;
        }

        for (int i = 0; i < NUM_BEACONS; i++) {
            struct beacon_data *node = &data_list[i];
            uint32_t target = ((uint32_t)node->major << 16) | node->minor;
            if (get_rb_node(target) == NULL) {
                continue;
            }

            struct distance_ring *buf = &node->distance_buffer;
            if (buf->count == 0) {
                continue;
            }

            double sum = 0;
            int sum_rssi = 0;
            int fresh = 0;
            uint64_t time = k_uptime_get();
            for (int j = 0; j < buf->count; j++) {
                if ((time - buf->timestamp[j]) <= 1000) {
                    sum += buf->distance[j];
                    sum_rssi += buf->rssi[j]; // average the rssi alongside
                    fresh++;
                }
            }

            if (fresh == 0) {
                continue;
            }

            distances[index] = (sum / fresh);
            readings[index] = (int8_t)(sum_rssi / fresh);
            bx[index] = node->x_corr;
            by[index] = node->y_corr;
            cali[index] = node->cali;
            ids[index] = target;
            index++;
        }

        // Done collecting data -> unlock tree
        rb_unlock();

        /* ------------------------------------------------------------------ */
        /* Multilateration: swap strongest beacon to index 0 as reference,    */
        /* then solve for position. Push result to localisation_msg_queue.    */
        /* ------------------------------------------------------------------ */
        if (index < 4) {
            // Not enough nodes to get a good postion calculation
            continue;
        }

        // Find best signal
        int best = 0;
        for (int i = 1; i < index; i++) {
            if (distances[i] < distances[best]) {
                best = i;
            }
        }

        /* Swap best signal into slot 0 */

        // Swap distance
        double tmp_d = distances[0];
        distances[0] = distances[best];
        distances[best] = tmp_d;
        // Swap calibration
        int8_t tmp_r = cali[0];
        cali[0] = cali[best];
        cali[best] = tmp_r;
        // Swap rssi reading
        tmp_r = readings[0];
        readings[0] = readings[best];
        readings[best] = tmp_r;
        // Swap node ID
        uint32_t tmp_id = ids[0];
        ids[0] = ids[best];
        ids[best] = tmp_id;
        // Swap static "x" position
        int32_t temp_d = bx[0];
        bx[0] = bx[best];
        bx[best] = temp_d;
        // Swap static "y" position
        temp_d = by[0];
        by[0] = by[best];
        by[best] = temp_d;

        // Calculate multilateration using the collected data
        int err = multilateration(distances, ids, bx, by, cali, index, &out_x, &out_y);
        if (err == 0) {
            // Valid result -> Timestamp and send forward for processing
            pos_data.timestamp = k_uptime_get();
            pos_data.x = out_x;
            pos_data.y = out_y;
            pos_data.len = index;
            for (int i = 0; i < index; i++) {
                pos_data.ids[i] = ids[i];
                pos_data.readings[i] = readings[i];
            }

            k_msgq_put(&localisation_msg_queue, &pos_data, K_NO_WAIT);
        }
    }
}

K_THREAD_DEFINE(localisation_thread, 8192, thread_localisation, NULL, NULL, NULL, 7, 0, 0);
