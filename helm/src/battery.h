/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <zephyr/kernel.h>

int get_battery_voltage(double *value);
int get_battery_charge(int32_t *value);

#endif
