// /* ========================================================================== */
// /* Kalman Filter Algorithm Source                                             */
// /* Written: Muhammed A                                                        */
// /* ========================================================================== */

// #include "kalman-m.h"

// #include <stdlib.h>
// #include <math.h>
// #include <zephyr/kernel.h>

// #define KALMAN_DT 0.1

// // A lot of these seem really as if they came out of nowhere, but they're the
// // process of trying to simplify the arithmetic of matrix multiplications
// // as significantly as possible. For example, calculating the P array in 
// // kf_predict requires calculating F * P * F^T - but since F is a constant
// // (and a simple one at that), it is very significantly faster, safer, and 
// // much more memory efficient to just simplify it. These simplifications were
// // done using https://www.matrixmultiplicationscalculator.com/ because I don't
// // trust my matrix multiplication skills lol

// static const double kalman_dt = KALMAN_DT;
// static const double kalman_dt2 = pow(KALMAN_DT, 2);

// void kf_init(struct kalman *kf, double Q, double R, double x0)
// {
//     kf->Q = Q;
//     kf->R = R;

//     // Starting at 0 (this is only for one axis)
//     kf->x_k[0] = x0;
//     kf->x_k[1] = 0;

//     // We just want a simple P matrix at the beginning since we have no 
//     // information about our sensor variance
//     kf->P[0][0] = 1;
//     kf->P[1][1] = 1;
//     kf->P[0][1] = 0;
//     kf->P[1][0] = 0;
// }

// // Predict first...
// void kf_predict(struct kalman *kf)
// {
//     // Constant velocity - just adds the velocity from last time on.
//     kf->x_k[0] += kalman_dt * kf->x_k[1];

//     kf->P[0][0] += kf->P[0][1] + kalman_dt * kf->P[1][0] +
//                    kalman_dt2 * kf->P[1][1] + kf->Q;
//     kf->P[0][1] += kalman_dt * kf->P[1][1];
//     kf->P[1][0] += kalman_dt * kf->P[1][1];
//     kf->P[1][1] += kf->Q;
// }

// // Get the values...
// // Then update:
// void kf_update(struct kalman *kf, double measurement)
// {
//     double K[2];
//     // By the miracle of matrix multiplication, the calculation for HPH^T
//     // literally just flattens to being the first item of the P array.
//     double K_scaling = 1 / (kf->P[0][0] + kf->R);
//     K[0] = K_scaling * kf->P[0][0];
//     K[1] = K_scaling * kf->P[1][0];

//     // Calculating x_update
//     kf->x_k[0] += K[0] * (measurement - kf->x_k[0]);
//     kf->x_k[1] += K[1] * (measurement - kf->x_k[0]);

//     // When calculating (I - K*H), it is of the form [1-K[0]  0], [-K[1]  1]
//     // Hence there are only two variables - here they are known as A and B,
//     // where A = 1 - K[0]; and B = -K[1];
//     double a = 1 - K[0];
//     double b = -K[1];

//     kf->P[1][0] += kf->P[0][0] * b;
//     kf->P[1][1] += kf->P[0][1] * b;

//     kf->P[0][0] *= a;
//     kf->P[0][1] *= a;
// }
