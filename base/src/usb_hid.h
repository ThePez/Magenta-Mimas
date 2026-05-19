/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <zephyr/kernel.h>
#include "zephyr/usb/class/hid.h"

enum hid_kbd_code translate_into_button(double speed, int8_t direction);

extern struct k_msgq hid_key_msgq;

#endif
