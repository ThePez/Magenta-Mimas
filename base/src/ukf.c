/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ukf.h"

#include "matrix.h"
#include "common.h"
#include "rb_tree.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#define BASE_CASE           0.02 * (GYRO_RING_BUF_SIZE)
#define PULSE_DELAY_DEFAULT 100
uint16_t PULSE_DELAY = PULSE_DELAY_DEFAULT;

K_MSGQ_DEFINE(kalman_msgq, sizeof(struct kalman), 5, 4);

void ukf_init(ukf_t *ukf)
{
    memset(ukf, 0, sizeof(ukf_t));

    const double alpha = ALPHA;
    const double beta = BETA;
    const double kappa = KAPPA;

    ukf->lambda = alpha * alpha * (NUM_STATES + kappa) - NUM_STATES;

    const double denom = NUM_STATES + ukf->lambda;

    /* initial covariance */
    ukf->P[0 * NUM_STATES + 0] = 1.0;
    ukf->P[1 * NUM_STATES + 1] = 0.1;
    ukf->P[2 * NUM_STATES + 2] = 0.1;

    /* process noise */
    ukf->Q[0 * NUM_STATES + 0] = Q_OMEGA; /* omega */
    ukf->Q[1 * NUM_STATES + 1] = BIAS_A;  /* biasA */
    ukf->Q[2 * NUM_STATES + 2] = BIAS_B;  /* biasB */

    /* measurement noise */
    ukf->R[0] = R_VAL;
    ukf->R[3] = R_VAL;

    /* ukf weights */
    ukf->wm[0] = ukf->lambda / denom;
    ukf->wc[0] = ukf->wm[0] + (1.0 - alpha * alpha + beta);

    for (uint8_t i = 1; i < SIGMA_POINTS; i++) {
        ukf->wm[i] = 1.0 / (2.0 * denom);
        ukf->wc[i] = ukf->wm[i];
    }
}

static int cholesky_decompose(double *A, double *L, uint8_t n)
{
    double sum;
    double d;
    memset(L, 0, sizeof(double) * n * n);

    for (uint8_t i = 0; i < n; i++) {
        for (uint8_t j = 0; j <= i; j++) {

            sum = 0.0;
            for (uint8_t k = 0; k < j; k++) {
                sum += L[i * n + k] * L[j * n + k];
            }
            if (i == j) {
                d = A[i * n + i] - sum;
                if (d <= 0.0) {
                    return (-EINVAL);
                }
                L[i * n + j] = sqrt(d);
            } else {
                L[i * n + j] = (A[i * n + j] - sum) / L[j * n + j];
            }
        }
    }

    return (0);
}

static int generate_sigma_points(ukf_t *ukf, double sigma[SIGMA_POINTS][NUM_STATES])
{
    double A[NUM_STATES * NUM_STATES];
    double L[NUM_STATES * NUM_STATES];
    double v;
    const double scale = NUM_STATES + ukf->lambda;

    /* A = (n + lambda) * P */
    for (uint8_t i = 0; i < NUM_STATES * NUM_STATES; i++) {
        A[i] = ukf->P[i] * scale;
    }

    /* Cholesky decomposition */
    if (cholesky_decompose(A, L, NUM_STATES) < 0) {
        return (-EINVAL);
    }

    /* central sigma points */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        sigma[0][i] = ukf->x[i];
    }

    /* remaining sigma points */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            v = L[j * NUM_STATES + i];
            sigma[i + 1][j] = ukf->x[j] + v;
            sigma[i + 1 + NUM_STATES][j] = ukf->x[j] - v;
        }
    }

    return (0);
}

/* assuming omega random -- which is want we want */
int ukf_predict(ukf_t *ukf)
{
    double sigma[SIGMA_POINTS][NUM_STATES];
    double dx[NUM_STATES];

    if (generate_sigma_points(ukf, sigma) < 0) {
        return (-EINVAL);
    }

    /* process model: omega(k + 1) = omega(k) -- small steps
    biases constant */

    /* predicted mean */
    memset(ukf->x, 0, sizeof(double) * NUM_STATES);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            ukf->x[j] += ukf->wm[i] * sigma[i][j];
        }
    }

    /* predicted covariance */
    memset(ukf->P, 0, sizeof(double) * NUM_STATES * NUM_STATES);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            dx[j] = sigma[i][j] - ukf->x[j];
        }
        for (uint8_t k = 0; k < NUM_STATES; k++) {
            for (uint8_t p = 0; p < NUM_STATES; p++) {
                ukf->P[k * NUM_STATES + p] += ukf->wc[i] * dx[k] * dx[p];
            }
        }
    }

    /* add process noise */
    for (uint8_t i = 0; i < NUM_STATES * NUM_STATES; i++) {
        ukf->P[i] += ukf->Q[i];
    }

    return (0);
}

int ukf_update(ukf_t *ukf, double aA, double aB)
{
    double sigma[SIGMA_POINTS][NUM_STATES];
    double z_sigma[SIGMA_POINTS][NUM_MEAS];
    double z_pred[NUM_MEAS];
    double S[NUM_MEAS][NUM_MEAS];
    double S_inv[NUM_MEAS][NUM_MEAS];
    double dz[NUM_MEAS];
    double dx[NUM_STATES];
    double Tc[NUM_STATES][NUM_MEAS];
    double K[NUM_STATES][NUM_MEAS];
    double residual[NUM_MEAS];
    double KS[NUM_STATES][NUM_MEAS];
    double KSKT[NUM_STATES][NUM_STATES];
    double omega;
    double biasA;
    double biasB;
    double ac;

    if (generate_sigma_points(ukf, sigma) < 0) {
        return (-EINVAL);
    }

    /* transform sigma points into measurement space */
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        omega = sigma[i][0];
        biasA = sigma[i][1];
        biasB = sigma[i][2];
        ac = RADIUS * omega * omega;
        z_sigma[i][0] = ac + biasA;
        z_sigma[i][1] = ac + biasB;
    }

    /* predicted measurement mean */
    memset(z_pred, 0, sizeof(double) * NUM_MEAS);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        for (uint8_t j = 0; j < NUM_MEAS; j++) {
            z_pred[j] += ukf->wm[i] * z_sigma[i][j];
        }
    }

    /* innovation covariance */
    memset(S, 0, sizeof(double) * NUM_MEAS * NUM_MEAS);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        dz[0] = z_sigma[i][0] - z_pred[0];
        dz[1] = z_sigma[i][1] - z_pred[1];
        for (uint8_t j = 0; j < NUM_MEAS; j++) {
            for (uint8_t k = 0; k < NUM_MEAS; k++) {
                S[j][k] += ukf->wc[i] * dz[j] * dz[k];
            }
        }
    }

    S[0][0] += ukf->R[0];
    S[1][1] += ukf->R[3];

    /* invert S */
    if (matrix2x2_inv(S, S_inv) < 0) {
        return (-EINVAL);
    }

    /* cross covariance */
    memset(Tc, 0, sizeof(double) * NUM_STATES * NUM_MEAS);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            dx[j] = sigma[i][j] - ukf->x[j];
        }
        dz[0] = z_sigma[i][0] - z_pred[0];
        dz[1] = z_sigma[i][1] - z_pred[1];
        for (uint8_t k = 0; k < NUM_STATES; k++) {
            for (uint8_t p = 0; p < NUM_MEAS; p++) {
                Tc[k][p] += ukf->wc[i] * dx[k] * dz[p];
            }
        }
    }

    /* kalman gain */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_MEAS; j++) {
            K[i][j] = Tc[i][0] * S_inv[0][j] + Tc[i][1] * S_inv[1][j];
        }
    }

    /* residuals */
    residual[0] = aA - z_pred[0];
    residual[1] = aB - z_pred[1];

    /* state update */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        ukf->x[i] += K[i][0] * residual[0] + K[i][1] * residual[1];
    }

    /* covariance update
    P = P - KSK^T */
    memset(KS, 0, sizeof(double) * NUM_STATES * NUM_MEAS);
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_MEAS; j++) {
            KS[i][j] = K[i][0] * S[0][j] + K[i][1] * S[1][j];
        }
    }
    memset(KSKT, 0, sizeof(double) * NUM_STATES * NUM_STATES);
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            KSKT[i][j] = KS[i][0] * K[j][0] + KS[i][1] * K[j][1];
        }
    }
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            ukf->P[i * NUM_STATES + j] -= KSKT[i][j];
        }
    }

    return (0);
}

void add_gyro_sample(struct gyro_ring *buf, double sample)
{

    buf->val[buf->head] = sample;
    buf->head = (buf->head + 1) % GYRO_RING_BUF_SIZE;
    if (buf->count < GYRO_RING_BUF_SIZE) {
        buf->count++;
    }
}

uint8_t gyro_moving_average(struct gyro_ring *buf)
{
    double sum = 0;

    for (uint8_t i = 0; i < buf->count; i++) {
        sum += fabs(buf->val[i]);
    }

    return (sum < BASE_CASE);
}

void thread_kalman(void *dummy1, void *dummy2, void *dummy3)
{
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);

    ukf_t ukf;
    double accelA;
    double accelB;
    double gyroA;
    double gyroB;
    double avg_gyro;
    double ac;
    double omega;
    int err;
    uint8_t initialised = 0;
    struct kalman results;

    struct gyro_ring buf = {.val = {0}, .head = 0, .count = 0};
    ukf_init(&ukf);

    while (1) {
        k_sem_take(&sensor_semaphore, K_FOREVER);

        rb_lock();
        struct helm_node *helm_a = get_rb_node(0);
        struct helm_node *helm_b = get_rb_node(1);

        accelA = helm_a->imu_data.accel_ms2;
        accelB = helm_b->imu_data.accel_ms2;
        gyroA = helm_a->imu_data.gyro_rads;
        gyroB = helm_b->imu_data.gyro_rads;

        rb_unlock();

        if (!initialised) {
            ac = 0.5 * (accelA + accelB);
            if (ac > 0.0) {
                ukf.x[0] = sqrt(ac / RADIUS);
                initialised = true;
            }
            continue;
        }

        avg_gyro = 0.5 * (gyroA + gyroB);
        add_gyro_sample(&buf, avg_gyro);

        err = ukf_predict(&ukf);
        if (err < 0) {
            printk("[ERROR] UKF Predict funtion %d\n", err);
            continue;
        }
        err = ukf_update(&ukf, accelA, accelB);
        if (err < 0) {
            printk("[ERROR] UKF  Update funtion %d\n", err);
            continue;
        }

        if (gyro_moving_average(&buf)) {
            ukf_init(&ukf);
            initialised = false;
        }

        omega = ukf.x[0];

        /* clockwise is negative, anti-clockwise is positive */
        results.magntidue = (omega * omega * RADIUS);

        if (avg_gyro < 0) {
            results.direction = -1;
        } else {
            results.direction = 1;
        }

        k_msgq_put(&kalman_msgq, &results, K_NO_WAIT);
    }
}

K_THREAD_DEFINE(kalman_thread, 8192, thread_kalman, NULL, NULL, NULL, 7, 0, 0);
