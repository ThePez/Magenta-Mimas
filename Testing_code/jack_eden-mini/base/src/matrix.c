/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <sys/errno.h>

// Various matrix functions for generic sizes

/* MxN * NxP = MxP */
void matrix_multi(double *A, double *B, double *C, int m, int n, int p)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < p; j++) {
            C[i * p + j] = 0;
            for (int k = 0; k < n; k++) {
                C[i * p + j] += A[i * n + k] * B[k * p + j];
            }
        }
    }
}

/* Transpose a matrix MxN -> NxM*/
void matrix_trans(double *A, double *B, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            B[j * m + i] = A[i * n + j];
        }
    }
}

/* Add two MxN matrices */
void matrix_add(double *A, double *B, double *C, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            C[i * n + j] = A[i * n + j] + B[i * n + j];
        }
    }
}

/* Sub two MxN matrices */
void matrix_sub(double *A, double *B, double *C, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            C[i * n + j] = A[i * n + j] - B[i * n + j];
        }
    }
}

/* Copy source matrix into destination matrix for MxN matrix */
void matrix_copy(double *src, double *dest, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            dest[i * n + j] = src[i * n + j];
        }
    }
}

/* Invert 2x2 matrix */
int matrix2x2_inv(double A[][2], double inv[][2])
{
    // [a b]^-1 = 1/det * [ d -b]
    // [c d]               [-c  a]

    double det = A[0][0] * A[1][1] - A[0][1] * A[1][0];

    if (fabs(det) < 1e-6) {
        // Singular matrix
        return (-EINVAL);
    }

    inv[0][0] = A[1][1] * (1.0 / det);
    inv[0][1] = -A[0][1] * (1.0 / det);
    inv[1][0] = -A[1][0] * (1.0 / det);
    inv[1][1] = A[0][0] * (1.0 / det);
    return (0);
}
