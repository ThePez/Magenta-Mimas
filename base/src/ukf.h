/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UKF_H
#define UKF_H

#include <stdint.h>

#define NUM_STATES   3
#define NUM_MEAS     2
#define SIGMA_POINTS (2 * NUM_STATES + 1)

extern uint16_t PULSE_DELAY;

#define Q_OMEGA 0.1 /* higher value more unreliable system*/
#define BIAS_A  1e-3
#define BIAS_B  1e-3
#define R_VAL   0.25 /* higher for untrustworthy sensor*/
#define ALPHA   0.3
#define BETA    2.0
#define KAPPA   0.0

#define GYRO_RING_BUF_SIZE 10

typedef struct {
    double x[NUM_STATES];              /* state matrix*/
    double P[NUM_STATES * NUM_STATES]; /* state error covariance matrix */
    double Q[NUM_STATES * NUM_STATES]; /* process noise covariance matrix */
    double R[NUM_MEAS * NUM_MEAS];     /* measurement noise covariance matrix */
    double lambda;                     /* signma point spread */
    double wm[SIGMA_POINTS];           /* weights for mean -> which sigma values matter more*/
    double wc[SIGMA_POINTS];           /* weights for covariance -> how uncertain is this shit*/
} ukf_t;

struct kalman {
    double magntidue;
    signed char direction;
};

extern struct k_msgq kalman_msgq; 

struct gyro_ring {
    double val[GYRO_RING_BUF_SIZE];
    uint8_t head;
    uint8_t count;
};

void ukf_init(ukf_t *ukf);
int ukf_predict(ukf_t *ukf);
int ukf_update(ukf_t *ukf, double aA, double aB);

#endif
