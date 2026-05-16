/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* TODO: I have bullshited all the numbers... */

#include "ukf.h"
#include "matrix.h"
#include <math.h>
#include <string.h>

void wheel_measurement(double *state, double *a_out)
{
    double omega = state[0];
    double biasA = state[2];
    double biasB = state[3];

    double ac = RADIUS * omega * omega;

    a_out[0] = ac + biasA;
    a_out[1] = ac + biasB;
}

void ukf_init(ukf_t *ukf)
{
    memset(ukf, 0, sizeof(ukf_t));

    ukf->lambda = LAMBDA;

    /* initial covariance */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        ukf->P[i * NUM_STATES + i] = 1.0;
    }

    /* process noise */
    ukf->Q[0] = Q_OMEGA; /* omega */
    ukf->Q[5] = Q_ACCEL; /* accel */
    ukf->Q[10] = BIAS_A; /* biasA */
    ukf->Q[15] = BIAS_B; /* biasB */

    /* measurement noise */
    ukf->R[0] = R;
    ukf->R[3] = R;

    double denom = NUM_STATES + ukf->lambda;

    ukf->wm[0] = ukf->lambda / denom;
    ukf->wc[0] = ukf->wm[0];

    for (uint8_t i = 1; i < SIGMA_POINTS; i++) {
        ukf->wm[i] = 1.0 / (2.0 * denom);
        ukf->wc[i] = ukf->wm[i];
    }
}

void generate_sigma_points(ukf_t *ukf, double sigma[SIGMA_POINTS][NUM_STATES])
{
    double scale = sqrt(NUM_STATES + ukf->lambda);

    sigma[0][0] = ukf->x[0];
    sigma[0][1] = ukf->x[1];
    sigma[0][2] = ukf->x[2];
    sigma[0][3] = ukf->x[3];

    for (uint8_t i = 0; i < NUM_STATES; i++) {

        double s = sqrt(ukf->P[i * NUM_STATES + i]) * scale;

        for (uint8_t j = 0; j < NUM_STATES; j++) {
            sigma[i + 1][j] = ukf->x[j];
            sigma[i + 1 + NUM_STATES][j] = ukf->x[j];
        }

        sigma[i + 1][i] += s;
        sigma[i + 1 + NUM_STATES][i] -= s;
    }
}

void ukf_predict(ukf_t *ukf, double dt)
{
    double sigma[SIGMA_POINTS][NUM_STATES];

    generate_sigma_points(ukf, sigma);

    /* propagate sigma points */
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        sigma[i][0] += sigma[i][1] * dt;
        /* sigma[i][1] = biasA -> for now assuming near constant bias
        sigma[i][2] = biasB */
    }

    /* mean */
    for (uint8_t j = 0; j < NUM_STATES; j++) {

        ukf->x[j] = 0.0;

        for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
            ukf->x[j] += ukf->wm[i] * sigma[i][j];
        }
    }

    /* covariance */
    memset(ukf->P, 0, sizeof(double) * NUM_STATES * NUM_STATES);
    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {

        double dx[NUM_STATES];

        for (uint8_t j = 0; j < NUM_STATES; j++) {
            dx[j] = sigma[i][j] - ukf->x[j];
        }

        for (uint8_t k = 0; k < NUM_STATES; k++) {
            for (uint8_t p = 0; p < NUM_STATES; p++) {
                ukf->P[k * NUM_STATES + p] += ukf->wc[i] * dx[k] * dx[p];
            }
        }
    }

    /* process noise */
    for (uint8_t i = 0; i < NUM_STATES * NUM_STATES; i++) {
        ukf->P[i] += ukf->Q[i];
    }
}

int ukf_update(ukf_t *ukf, double aA, double aB)
{
    double sigma[SIGMA_POINTS][NUM_STATES];
    generate_sigma_points(ukf, sigma);
    double a_sigma[SIGMA_POINTS][NUM_MEAS];

    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        wheel_measurement(sigma[i], a_sigma[i]);
    }

    /* predicte measurement mean */
    double a_pred[2] = {0};

    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {
        a_pred[0] += ukf->wm[i] * a_sigma[i][0];
        a_pred[1] += ukf->wm[i] * a_sigma[i][1];
    }

    /* covariance */
    double S[2][2] = {0};

    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {

        double dz0 = a_sigma[i][0] - a_pred[0];
        double dz1 = a_sigma[i][1] - a_pred[1];

        S[0][0] += ukf->wc[i] * dz0 * dz0;
        S[0][1] += ukf->wc[i] * dz0 * dz1;
        S[1][0] += ukf->wc[i] * dz1 * dz0;
        S[1][1] += ukf->wc[i] * dz1 * dz1;
    }

    S[0][0] += ukf->R[0];
    S[1][1] += ukf->R[3];

    double S_inv[2][2];
    int err = matrix2x2_inv(S, S_inv);
    if (err < 0) {
        return (err);
    }

    /* cross covariance */
    double Tc[NUM_STATES][2] = {0};

    for (uint8_t i = 0; i < SIGMA_POINTS; i++) {

        double dx[NUM_STATES];
        double da[2];

        for (int j = 0; j < NUM_STATES; j++) {
            dx[j] = sigma[i][j] - ukf->x[j];
        }

        da[0] = a_sigma[i][0] - a_pred[0];
        da[1] = a_sigma[i][1] - a_pred[1];

        for (uint8_t j = 0; j < NUM_STATES; j++) {
            Tc[j][0] += ukf->wc[i] * dx[j] * da[0];
            Tc[j][1] += ukf->wc[i] * dx[j] * da[1];
        }
    }

    /* kalman gain */
    double K[NUM_STATES][2];

    for (uint8_t i = 0; i < NUM_STATES; i++) {
        K[i][0] = Tc[i][0] * S_inv[0][0] + Tc[i][1] * S_inv[1][0];
        K[i][1] = Tc[i][0] * S_inv[0][1] + Tc_[i][1] * S_inv[1][1];
    }

    /* residual */
    double residual[2];

    residual[0] = aA - a_pred[0];
    residual[1] = aB - a_pred[1];

    /* state update */
    for (uint8_t i = 0; i < NUM_STATES; i++) {
        ukf->x[i] += K[i][0] * residual[0] + K[i][1] * residual[1];
    }

    return (0);
}

/* for the thread... */
/*

    ukf_t ukf;
    ukf_init(&ukf);

    while(1) {

        double accelA = get accel A data;
        double accelB = get accel B data;

        remove gravity
        double ac = 0.5 * (accelA + accelB);

        ukf_predict(&ukf, 0.001);
        int err = ukf_update(&ukf, ac);
        if (err < 0) {
            sad face
        }

        double omega = ukf.x[0];

        double centripetal = omega * omega * RADIUS;

    }

*/
