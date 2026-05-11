/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <sys/errno.h>

/**
 * @brief Multiply two matrices: (MxN) * (NxP) = (MxP).
 *
 * @param A  Left-hand input matrix, stored row-major with dimensions [m][n].
 * @param B  Right-hand input matrix, stored row-major with dimensions [n][p].
 * @param C  Output matrix, stored row-major with dimensions [m][p]. Must not
 *           alias A or B.
 * @param m  Number of rows in A and C.
 * @param n  Number of columns in A and rows in B.
 * @param p  Number of columns in B and C.
 */
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

/**
 * @brief Transpose a matrix: (MxN) -> (NxM).
 *
 * @param A  Input matrix, stored row-major with dimensions [m][n].
 * @param B  Output transposed matrix, stored row-major with dimensions [n][m].
 *           Must not alias A.
 * @param m  Number of rows in A (columns in B).
 * @param n  Number of columns in A (rows in B).
 */
void matrix_trans(double *A, double *B, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            B[j * m + i] = A[i * n + j];
        }
    }
}

/**
 * @brief Element-wise addition of two MxN matrices: C = A + B.
 *
 * @param A  First input matrix, stored row-major with dimensions [m][n].
 * @param B  Second input matrix, stored row-major with dimensions [m][n].
 * @param C  Output matrix, stored row-major with dimensions [m][n].
 * @param m  Number of rows.
 * @param n  Number of columns.
 */
void matrix_add(double *A, double *B, double *C, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            C[i * n + j] = A[i * n + j] + B[i * n + j];
        }
    }
}

/**
 * @brief Element-wise subtraction of two MxN matrices: C = A - B.
 *
 * @param A  Minuend matrix, stored row-major with dimensions [m][n].
 * @param B  Subtrahend matrix, stored row-major with dimensions [m][n].
 * @param C  Output matrix, stored row-major with dimensions [m][n].
 * @param m  Number of rows.
 * @param n  Number of columns.
 */
void matrix_sub(double *A, double *B, double *C, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            C[i * n + j] = A[i * n + j] - B[i * n + j];
        }
    }
}

/**
 * @brief Copy an MxN matrix element-wise: dest = src.
 *
 * @param src   Source matrix, stored row-major with dimensions [m][n].
 * @param dest  Destination matrix, stored row-major with dimensions [m][n].
 *              Must not alias src.
 * @param m     Number of rows.
 * @param n     Number of columns.
 */
void matrix_copy(double *src, double *dest, int m, int n)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            dest[i * n + j] = src[i * n + j];
        }
    }
}

/**
 * @brief Compute the inverse of a 2x2 matrix.
 *
 * Uses the closed-form adjugate formula: A^-1 = (1/det) * [[d,-b],[-c,a]].
 * Returns an error if the matrix is singular (|det| < 1e-6).
 *
 * @param A    Input 2x2 matrix.
 * @param inv  Output 2x2 inverse matrix. Must not alias A.
 * @return 0 on success, -EINVAL if A is singular.
 */
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
