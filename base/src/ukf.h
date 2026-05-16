/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #ifndef UKF_H
 #define UKF_H

 #define NUM_STATES 4
 #define NUM_MEAS 2
 #define SIGMA_POINTS (2 * NUM_STATES + 1)

 #define RADIUS 0.4
 #define Q_OMEGA 0.01 /* higher value more unreliable system*/
 #define Q_ACCEL 0.1 /* higher value more unreliable system*/
 #define BIAS_A 1e-5
 #define BIAS_B 1e-5
 #define R 0.5 /* higher for untrustworthy sensor*/
 #define LAMBDA 1.0 /* higher for more non-linearity */

 typedef struct {
    double x[NUM_STATES]; /* state matrix*/
    double P[NUM_STATES * NUM_STATES]; /* state error covariance matrix */
    double Q[NUM_STATES * NUM_STATES]; /* process noise covariance matrix */
    double R[NUM_MEAS * NUM_MEAS]; /* measurement noise covariance matrix */
    double lambda; /* signma point spread */
    double wm[SIGMA_POINTS]; /* weights for mean -> which sigma values matter more*/
    double wc[SIGMA_POINTS]; /* weights for covariance -> how uncertain is this shit*/
} ukf_t;

void ukf_init(ukf_t *ukf);
void ukf_predict(ukf_t *ukf, double dt);
void ukf_update(ukf_t *ukf, double aA, double aB);
void wheel_measurement(double *state, double *a_out);

#endif