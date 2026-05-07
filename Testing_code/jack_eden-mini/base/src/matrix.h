/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MATRIX_H
#define MATRIX_H

void matrix_multi(double *A, double *B, double *C, int m, int n, int p);
void matrix_trans(double *A, double *B, int m, int n);
void matrix_add(double *A, double *B, double *C, int m, int n);
void matrix_sub(double *A, double *B, double *C, int m, int n);
void matrix_copy(double *src, double *dest, int m, int n);
int matrix2x2_inv(double A[][2], double inv[][2]);

#endif
