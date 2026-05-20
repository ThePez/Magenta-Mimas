/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_hid.h"

#include "usbd_init.h"
#include "ukf.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define HID_THREAD_PRIORITY 5

// Speed options for keyboard output
#define THRESHOLD_A 3 
#define THRESHOLD_B 10
#define THRESHOLD_C 20
#define THRESHOLD_D 40

// Keyboard output options
#define SPEED_0_DIR_0 BIT(0) /* 1  */
#define SPEED_1_DIR_0 BIT(1) /* 2  */
#define SPEED_2_DIR_0 BIT(2) /* 4  */
#define SPEED_3_DIR_0 BIT(3) /* 8  */
#define SPEED_0_DIR_1 BIT(4) /* 16 */
#define SPEED_1_DIR_1 BIT(5) /* 32 */
#define SPEED_2_DIR_1 BIT(6) /* 64 */
#define SPEED_3_DIR_1 BIT(7) /* 128 */

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

/* Standard HID boot-keyboard report descriptor */
static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

/* Byte offsets within the 8-byte USB HID keyboard report */
enum kb_report_idx {
    KB_MOD_KEY = 0, /* modifier keys (shift, ctrl, alt, ...) */
    KB_RESERVED,    /* reserved byte - always 0 per HID spec */
    KB_KEY_CODE1,   /* first key code (we use only this slot) */
    KB_KEY_CODE2,
    KB_KEY_CODE3,
    KB_KEY_CODE4,
    KB_KEY_CODE5,
    KB_KEY_CODE6,
    KB_REPORT_COUNT, /* total report length in bytes */
};

/* Keycode queue fed by other threads; hid_thread drains it */
K_MSGQ_DEFINE(hid_key_msgq, sizeof(enum hid_kbd_code), 8, 1);

/* DMA-safe report buffer - must stay allocated for the lifetime of each transfer */
UDC_STATIC_BUF_DEFINE(report, KB_REPORT_COUNT);
static uint32_t kb_duration; /* current idle duration set by the host */
static bool kb_ready;        /* true once the HID interface is enumerated */

/* ========================================================================== */
/* HID Callbacks                                                              */
/* ========================================================================== */

static void kb_iface_ready(const struct device *dev, const bool ready)
{
    // printk("HID device %s interface is %s\n", dev->name, ready ? "ready" : "not ready");
    kb_ready = ready;
}

/* GET_REPORT request - not needed for a simple keyboard, return success */
static int kb_get_report(const struct device *dev, const uint8_t type, const uint8_t id,
                         const uint16_t len, uint8_t *const buf)
{
    return 0;
}

/* SET_REPORT request - not needed for a simple keyboard, return success */
static int kb_set_report(const struct device *dev, const uint8_t type, const uint8_t id,
                         const uint16_t len, const uint8_t *const buf)
{
    return 0;
}

static void kb_set_idle(const struct device *dev, const uint8_t id, const uint32_t duration)
{
    kb_duration = duration;
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id)
{
    return kb_duration;
}

/* SET_PROTOCOL - not needed for a simple keyboard, return */
static void kb_set_protocol(const struct device *dev, const uint8_t proto)
{
}

/* Output report from host - not needed for a simple keyboard, return */
static void kb_output_report(const struct device *dev, const uint16_t len, const uint8_t *const buf)
{
}

/* Stores all the function pointer for the HID stack */
static struct hid_device_ops kb_ops = {
    .iface_ready = kb_iface_ready, // Literally only this is needed
    .get_report = kb_get_report,
    .set_report = kb_set_report,
    .set_idle = kb_set_idle,
    .get_idle = kb_get_idle,
    .set_protocol = kb_set_protocol,
    .output_report = kb_output_report,
};

/* ========================================================================== */
/* USB Message Handling                                                       */
/* ========================================================================== */

/* USB stack event callback - enables/disables the device on VBUS attach/detach */
static void msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
    printk("USBD message: %s\n", usbd_msg_type_string(msg->type));

    if (usbd_can_detect_vbus(ctx)) {
        if (msg->type == USBD_MSG_VBUS_READY) {
            if (usbd_enable(ctx)) {
                printk("Failed to enable device support\n");
            }
        }
        if (msg->type == USBD_MSG_VBUS_REMOVED) {
            if (usbd_disable(ctx)) {
                printk("Failed to disable device support\n");
            }
        }
    }
}

/* ========================================================================== */
/* Keyboard Mapping                                                           */
/* ========================================================================== */

/* Map the inputs of speed and direction to 8 different output keys */
enum hid_kbd_code translate_into_button(double speed, int8_t direction)
{
    speed *= 8;

    if (speed < THRESHOLD_A) {
        return HID_KEY_G;
    }
    // Map Speed into 4 options
    uint8_t code = 1;
    if (speed < THRESHOLD_B) {
        code <<= 0;
    } else if (speed < THRESHOLD_C) {
        code <<= 1;
    } else if (speed < THRESHOLD_D) {
        code <<= 2;
    } else {
        code <<= 3;
    }

    // Map direction into the 2 groups of 4 speeds
    code <<= (direction == -1) ? 4 : 0;

    // Output the desired key
    switch (code) {
    case SPEED_0_DIR_0:
        return HID_KEY_F;
    case SPEED_1_DIR_0:
        return HID_KEY_D;
    case SPEED_2_DIR_0:
        return HID_KEY_S;
    case SPEED_3_DIR_0:
        return HID_KEY_A;
    case SPEED_0_DIR_1:
        return HID_KEY_H;
    case SPEED_1_DIR_1:
        return HID_KEY_J;
    case SPEED_2_DIR_1:
        return HID_KEY_K;
    case SPEED_3_DIR_1:
        return HID_KEY_L;
    default:
        // Shouldn't be possible
        return HID_KEY_G;
    }
}

/* ========================================================================== */
/* HID Thread                                                                 */
/* ========================================================================== */

static void hid_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *hid_dev;
    struct usbd_context *usbd_ctx;
    uint8_t keycode;
    int ret;

    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
    if (!device_is_ready(hid_dev)) {
        printk("[ERROR] HID device not ready\n");
        return;
    }

    // Register the keyboard report descriptor and ops before enabling USB
    ret = hid_device_register(hid_dev, hid_report_desc, sizeof(hid_report_desc), &kb_ops);
    if (ret) {
        printk("[ERROR] Failed to register HID device: %d\n", ret);
        return;
    }

    // Initialise the keyboard device
    usbd_ctx = usbd_init_device(msg_cb);
    if (usbd_ctx == NULL) {
        printk("[ERROR] Failed to initialize USB device\n");
        return;
    }

    // If VBUS detection is unavailable, enable immediately rather than waiting for msg_cb
    if (!usbd_can_detect_vbus(usbd_ctx)) {
        ret = usbd_enable(usbd_ctx);
        if (ret) {
            printk("[ERROR] Failed to enable USB: %d\n", ret);
            return;
        }
    }

    printk("[INFO] USB HID keyboard initialized\n");
    while (1) {
        k_msgq_get(&hid_key_msgq, &keycode, K_FOREVER);

        if (!kb_ready) {
            printk("[WARN] USB HID not ready, dropping keypress 0x%02x\n", keycode);
            continue;
        }

        // Send key-down report, wait for host debounce, then send key-up
        report[KB_KEY_CODE1] = keycode;
        if (hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report)) {
            printk("[WARN] HID submit error (key down)\n");
        }

        k_msleep(PULSE_DELAY);

        report[KB_KEY_CODE1] = 0;
        if (hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report)) {
            printk("[WARN] HID submit error (key up)\n");
        }
    }
}

K_THREAD_DEFINE(hid_tid, 2048, hid_thread, NULL, NULL, NULL, HID_THREAD_PRIORITY, 0, 0);
