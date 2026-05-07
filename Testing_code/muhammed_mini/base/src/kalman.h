/* ========================================================================== */
/* Kalman Filter Algorithm Header                                             */
/* Written: Muhammed A                                                        */
/* ========================================================================== */

#ifndef BASE_KALMAN_H
#define BASE_KALMAN_H

#include <zephyr/kernel.h>

// Default values (this was just trial and error)
#define KF_Q  0.5
#define KF_R  20
#define KF_X0 0

struct kalman {
    // F will always be [1 dt] [0 1]
    // H matrix is [1 0] 
    double Q;
    double R;

    double x_k[2];
    double P[2][2];
};

void kf_init(struct kalman *kf, double Q, double R, double x0);
void kf_predict(struct kalman *kf);
void kf_update(struct kalman *kf, double measurement);

#endif
