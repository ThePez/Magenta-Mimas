/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kalman.h"
#include "matrix.h"
#include "localisation.h"

#include "zephyr/kernel.h"
#include "zephyr/toolchain.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

K_MSGQ_DEFINE(kalman_msg_queue, sizeof(struct all_position), 5, 4);

/* ========================================================================== */
/* Filter State                                                               */
/* Static instance of the 2D Kalman filter. Matrices are zero-initialised     */
/* here; kf_init() fills in the correct values before the thread runs.        */
/* F and H are set to identity/observation defaults; Q, R, P are zeroed       */
/* and populated at runtime to allow variable-dt updates.                     */
/* ========================================================================== */

/* clang-format off */
struct kalman_filter_2D filter = {
    .F = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1}},
    .H = {
        {1, 0, 0, 0},
        {0, 1, 0, 0}},
    .Q = {
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},
    .R = {
        {0, 0},
        {0, 0}},
    .P = {
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},
    .x = {
        {0}, {0}, {0}, {0}}
};

/* Identity matrix reused in the covariance update step: P = (I - KH) * P */
double identity[NUM_STATES][NUM_STATES] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}
};
/* clang-format on */

/* ========================================================================== */
/* INITIALISATION                                                             */
/* Sets up F, Q, R, and P using the default DT defined in kalman.h.           */
/* Called once before the filter loop starts. F and Q will be rebuilt each    */
/* cycle by kf_update_dt() using the actual measured dt.                      */
/*                                                                            */
/* Tuning knobs (defined in kalman.h):                                        */
/*   Q_VAR - process noise variance. Increase if output lags real position.   */
/*   R_VAR - measurement noise variance. Increase if output is too noisy.     */
/*   P0    - initial state covariance diagonal. Decrease for faster           */
/*           convergence at startup.                                          */
/* ========================================================================== */

void kf_init(struct kalman_filter_2D *kf)
{
    double dt = DT;
    double dt2 = pow(DT, 2);
    double dt3 = pow(DT, 3);
    double dt4 = pow(DT, 4);

    /* State transition matrix F: constant-velocity model */
    /* x_k = x_{k-1} + vx*dt,  y_k = y_{k-1} + vy*dt      */
    kf->F[0][2] = dt;
    kf->F[1][3] = dt;

    /* Process noise covariance Q: derived from continuous white noise */
    /* acceleration model, discretised over dt.                        */
    kf->Q[0][0] = dt4 / 4 * Q_VAR;
    kf->Q[0][2] = dt3 / 2 * Q_VAR;
    kf->Q[1][1] = dt4 / 4 * Q_VAR;
    kf->Q[1][3] = dt3 / 2 * Q_VAR;
    kf->Q[2][0] = dt3 / 2 * Q_VAR;
    kf->Q[2][2] = dt2 * Q_VAR;
    kf->Q[3][1] = dt3 / 2 * Q_VAR;
    kf->Q[3][3] = dt2 * Q_VAR;

    /* Measurement noise covariance R: assumed diagonal (x and y */
    /* measurement errors are independent).                      */
    kf->R[0][0] = R_VAR;
    kf->R[1][1] = R_VAR;

    /* Initial state covariance P: diagonal with P0, reflecting high */
    /* initial uncertainty in both position and velocity states.     */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        for (uint8_t j = 0; j < NUM_STATES; j++) {
            kf->P[i][j] = (i == j) ? P0 : 0.0;
        }
    }
}

/* ========================================================================== */
/* UPDATE DT                                                                  */
/* Rebuilds F and Q for the actual elapsed time since the last measurement.   */
/* Must be called before kf_predict() each cycle so the motion model and      */
/* process noise correctly reflect the real sample interval rather than the   */
/* fixed DT fallback.                                                         */
/* ========================================================================== */

static void kf_update_dt(struct kalman_filter_2D *kf, double dt)
{
    double dt2 = dt * dt;
    double dt3 = dt2 * dt;
    double dt4 = dt3 * dt;

    /* Rebuild state transition matrix with new dt */
    kf->F[0][2] = dt;
    kf->F[1][3] = dt;

    /* Rebuild process noise covariance with new dt */
    kf->Q[0][0] = dt4 / 4 * Q_VAR;
    kf->Q[0][2] = dt3 / 2 * Q_VAR;
    kf->Q[1][1] = dt4 / 4 * Q_VAR;
    kf->Q[1][3] = dt3 / 2 * Q_VAR;
    kf->Q[2][0] = dt3 / 2 * Q_VAR;
    kf->Q[2][2] = dt2 * Q_VAR;
    kf->Q[3][1] = dt3 / 2 * Q_VAR;
    kf->Q[3][3] = dt2 * Q_VAR;
}

/* ========================================================================== */
/* PREDICT STEP                                                               */
/* Projects the state and covariance forward one timestep using the motion    */
/* model F and process noise Q.                                               */
/*                                                                            */
/*   x = F * x                                                                */
/*   P = F * P * F^T + Q                                                      */
/* ========================================================================== */

static void kf_predict(struct kalman_filter_2D *kf)
{
    /* x = F * x */
    matrix_multi(&(kf->F[0][0]), &(kf->x[0][0]), &(kf->x_prior[0][0]), NUM_STATES, NUM_STATES, 1);

    /* P = F * P * F^T + Q */
    double Ft[NUM_STATES][NUM_STATES];
    matrix_trans(&(kf->F[0][0]), &(Ft[0][0]), NUM_STATES, NUM_STATES);

    double FP[NUM_STATES][NUM_STATES];
    matrix_multi(&(kf->F[0][0]), &(kf->P[0][0]), &(FP[0][0]), NUM_STATES, NUM_STATES, NUM_STATES);

    double FPFt[NUM_STATES][NUM_STATES];
    matrix_multi(&(FP[0][0]), &(Ft[0][0]), &(FPFt[0][0]), NUM_STATES, NUM_STATES, NUM_STATES);

    matrix_add(&(FPFt[0][0]), &(kf->Q[0][0]), &(kf->P_prior[0][0]), NUM_STATES, NUM_STATES);

    /* Commit predicted state and covariance */
    matrix_copy(&(kf->x_prior[0][0]), &(kf->x[0][0]), NUM_STATES, 1);
    matrix_copy(&(kf->P_prior[0][0]), &(kf->P[0][0]), NUM_STATES, NUM_STATES);
}

/* ========================================================================== */
/* UPDATE STEP                                                                */
/* Corrects the predicted state using the new position measurement z.         */
/*                                                                            */
/*   y = z - H * x              (innovation)                                  */
/*   S = H * P * H^T + R        (innovation covariance)                       */
/*   K = P * H^T * S^-1         (Kalman gain)                                 */
/*   x = x + K * y              (state update)                                */
/*   P = (I - K * H) * P        (covariance update)                           */
/*                                                                            */
/* Returns -EINVAL if S is singular (measurement is discarded).               */
/* ========================================================================== */

static int kf_update(struct kalman_filter_2D *kf, double z[NUM_MEAS][1])
{
    int err;

    /* y = z - H * x */
    double Hx[NUM_MEAS][1];
    matrix_multi(&(kf->H[0][0]), &(kf->x[0][0]), &(Hx[0][0]), NUM_MEAS, NUM_STATES, 1);

    double y[NUM_MEAS][1];
    matrix_sub(&(z[0][0]), &(Hx[0][0]), &(y[0][0]), NUM_MEAS, 1);

    /* S = H * P * H^T + R */
    double Ht[NUM_STATES][NUM_MEAS];
    matrix_trans(&(kf->H[0][0]), &(Ht[0][0]), NUM_MEAS, NUM_STATES);

    double HP[NUM_MEAS][NUM_STATES];
    matrix_multi(&(kf->H[0][0]), &(kf->P[0][0]), &(HP[0][0]), NUM_MEAS, NUM_STATES, NUM_STATES);

    double S[NUM_MEAS][NUM_MEAS];
    matrix_multi(&(HP[0][0]), &(Ht[0][0]), &(S[0][0]), NUM_MEAS, NUM_STATES, NUM_MEAS);
    matrix_add(&(S[0][0]), &(kf->R[0][0]), &(S[0][0]), NUM_MEAS, NUM_MEAS);

    /* K = P * H^T * S^-1 */
    double S_inv[NUM_MEAS][NUM_MEAS];
    err = matrix2x2_inv(S, S_inv);
    if (err) {
        return (err);
    }

    double PHt[NUM_STATES][NUM_MEAS];
    matrix_multi(&(kf->P[0][0]), &(Ht[0][0]), &(PHt[0][0]), NUM_STATES, NUM_STATES, NUM_MEAS);
    matrix_multi(&(PHt[0][0]), &(S_inv[0][0]), &(kf->K[0][0]), NUM_STATES, NUM_MEAS, NUM_MEAS);

    /* x = x + K * y */
    double Ky[NUM_STATES][1];
    matrix_multi(&(kf->K[0][0]), &(y[0][0]), &(Ky[0][0]), NUM_STATES, NUM_MEAS, 1);
    matrix_add(&(kf->x[0][0]), &(Ky[0][0]), &(kf->x[0][0]), NUM_STATES, 1);

    /* P = (I - K * H) * P */
    double KH[NUM_STATES][NUM_STATES];
    matrix_multi(&(kf->K[0][0]), &(kf->H[0][0]), &(KH[0][0]), NUM_STATES, NUM_MEAS, NUM_STATES);

    double I_KH[NUM_STATES][NUM_STATES];
    matrix_sub(&(identity[0][0]), &(KH[0][0]), &(I_KH[0][0]), NUM_STATES, NUM_STATES);
    matrix_multi(&(I_KH[0][0]), &(kf->P[0][0]), &(kf->P[0][0]), NUM_STATES, NUM_STATES, NUM_STATES);

    return (0);
}

/* ========================================================================== */
/* KALMAN THREAD                                                              */
/* Consumes raw position fixes from localisation_msg_queue, runs one          */
/* predict-update cycle per fix, and pushes both the raw and filtered         */
/* positions onto kalman_msg_queue for the UART thread to transmit.           */
/*                                                                            */
/* dt is computed from the timestamp embedded in each position_data struct    */
/* rather than assuming a fixed interval. It is clamped to [0.05, 2.0]s to    */
/* prevent corruption from localisation stalls or burst catch-up.             */
/* ========================================================================== */

void thread_kalman(void *dummy1, void *dummy2, void *dummy3)
{
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);
    ARG_UNUSED(dummy3);

    struct position_data raw = {0};
    struct kalman_position kalman = {0};
    struct all_position msg = {0};
    double raw_pos[NUM_MEAS][1] = {0};

    int64_t last_time = 0;

    kf_init(&filter);

    while (1) {

        /* Block until a new position fix is available */
        k_msgq_get(&localisation_msg_queue, &raw, K_FOREVER);

        /* Compute actual dt from localisation timestamps */
        double dt;
        if (last_time == 0) {
            dt = DT; /* fallback for the very first measurement */
        } else {
            dt = (double)(raw.timestamp - last_time) / 1000.0;

            /* Clamp dt - a stall followed by burst catch-up would otherwise */
            /* cause an implausibly large state prediction jump              */
            if (dt > 2.0) {
                dt = 2.0;
            } else if (dt < 0.05) {
                dt = 0.05;
            }
        }

        last_time = raw.timestamp;

        raw_pos[0][0] = raw.x;
        raw_pos[1][0] = raw.y;

        /* Rebuild F and Q for this dt before predicting */
        kf_update_dt(&filter, dt);

        /* Predict then correct */
        kf_predict(&filter);
        if (kf_update(&filter, raw_pos) < 0) {
            // Singular matrix found -> discard this measurment
            continue;
        }

        /* store kalman output */
        kalman.x = filter.x[0][0];
        kalman.y = filter.x[1][0];
        kalman.timestamp = k_uptime_get();

        /* Forward both raw and kalman positions to the UART thread */
        msg.raw = raw;
        msg.kalman = kalman;
        k_msgq_put(&kalman_msg_queue, &msg, K_NO_WAIT);
    }
}

K_THREAD_DEFINE(kalman_thread, 8192, thread_kalman, NULL, NULL, NULL, 7, 0, 0);
