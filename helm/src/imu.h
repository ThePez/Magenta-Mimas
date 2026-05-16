/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMU_H
#define IMU_H

#include <zephyr/kernel.h>

extern struct k_msgq imu_q;

int initialise_imu(void);

#endif
