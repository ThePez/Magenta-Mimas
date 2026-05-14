/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USBD_INIT_H
#define USBD_INIT_H

#include <stdint.h>
#include <zephyr/usb/usbd.h>

/* It returns the configured and initialized USB device context on success,
 * otherwise it returns NULL. */
struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb);

#endif
