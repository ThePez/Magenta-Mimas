/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KALMAN_H
#define KALMAN_H

#include "localisation.h"

#define DT         0.5 // default fallback
#define NUM_STATES 4
#define NUM_MEAS   2

// Tuning Constants

/*
 -  R_VAR: how much you trust the multilateration output. Higher = filter trusts its own prediction
    more and smooths out measurement noise, but lags real movement.
 -  Q_VAR: how much you trust the motion model. Higher = filter responds faster to real movement,
    but passes through more noise.
 -  P0: just affects how quickly the filter converges from startup, not steady-state behaviour.
*/

#define R_VAR 50.0
#define Q_VAR 45.0
#define P0    5.0

/* Message Queue containing positional data */
extern struct k_msgq kalman_msg_queue;

struct kalman_position {
    int64_t timestamp;
    double x;
    double y;
};

struct all_position {
    struct position_data raw;
    struct kalman_position kalman;
};

struct kalman_filter_2D {
    double F[NUM_STATES][NUM_STATES]; /* state transition matrix*/
    double H[NUM_MEAS][NUM_STATES];
    double Q[NUM_STATES][NUM_STATES];
    double R[NUM_MEAS][NUM_MEAS];
    double P[NUM_STATES][NUM_STATES];
    double x[NUM_STATES][1];

    double x_prior[NUM_STATES][1];
    double P_prior[NUM_STATES][NUM_STATES];
    double K[NUM_STATES][NUM_MEAS];
};

#endif /* KALMAN_H */
