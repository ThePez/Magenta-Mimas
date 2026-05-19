/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usbd_init.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define USBD_MANUFACTURER "MAKING_WAVES"
#define USBD_PRODUCT      "WAVES HID"
#define USBD_PID          0x0007
#define USBD_MAX_POWER    125
#define USBD_VID          0x2fe3

/* ========================================================================== */
/* Static Data                                                                */
/* ========================================================================== */

/* Exclude the DFU mode instance - only DFU runtime class is needed */
static const char *const blocklist[] = {
    "dfu_dfu",
    NULL,
};

/* USB device context bound to the hardware UDC controller node */
USBD_DEVICE_DEFINE(base_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USBD_VID, USBD_PID);

/* String descriptors: language, manufacturer, product, and optional serial number */
USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(sn)));

/* Configuration descriptors for full-speed and high-speed operation */
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

static const uint8_t attributes = USB_SCD_SELF_POWERED;

/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(fs_config, attributes, USBD_MAX_POWER, &fs_cfg_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(hs_config, attributes, USBD_MAX_POWER, &hs_cfg_desc);

/* ========================================================================== */
/* USB Device Setup                                                           */
/* ========================================================================== */

// Does something....
static void sample_fix_code_triple(struct usbd_context *uds_ctx, const enum usbd_speed speed)
{
    if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) || IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) || IS_ENABLED(CONFIG_USBD_MIDI2_CLASS) ||
        IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS) || IS_ENABLED(CONFIG_USBD_VIDEO_CLASS)) {
        /*
         * Class with multiple interfaces have an Interface
         * Association Descriptor available, use an appropriate triple
         * to indicate it.
         */
        usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    } else {
        usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
    }
}

// Sets up the device
struct usbd_context *usbd_setup_device(usbd_msg_cb_t msg_cb)
{
    int err;

    // Add string descriptors so the host can identify the device
    err = usbd_add_descriptor(&base_usbd, &lang);
    if (err) {
        printk("[ERROR] Failed to initialize language descriptor (%d)\n", err);
        return NULL;
    }

    err = usbd_add_descriptor(&base_usbd, &mfr);
    if (err) {
        printk("[ERROR] Failed to initialize manufacturer descriptor (%d)\n", err);
        return NULL;
    }

    err = usbd_add_descriptor(&base_usbd, &product);
    if (err) {
        printk("[ERROR] Failed to initialize product descriptor (%d)\n", err);
        return NULL;
    }

    IF_ENABLED(CONFIG_HWINFO, (
		err = usbd_add_descriptor(&base_usbd, &sn);
	))
    if (err) {
        printk("[ERROR] Failed to initialize SN descriptor (%d)\n", err);
        return NULL;
    }

    // Add high-speed configuration only when the hardware actually supports HS
    if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&base_usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&base_usbd, USBD_SPEED_HS, &hs_config);
        if (err) {
            printk("[ERROR] Failed to add High-Speed configuration\n");
            return NULL;
        }

        err = usbd_register_all_classes(&base_usbd, USBD_SPEED_HS, 1, blocklist);
        if (err) {
            printk("[ERROR] Failed to add register classes\n");
            return NULL;
        }

        sample_fix_code_triple(&base_usbd, USBD_SPEED_HS);
    }

    // Always add full-speed configuration as the baseline fallback
    err = usbd_add_configuration(&base_usbd, USBD_SPEED_FS, &fs_config);
    if (err) {
        printk("[ERROR] Failed to add Full-Speed configuration\n");
        return NULL;
    }

    err = usbd_register_all_classes(&base_usbd, USBD_SPEED_FS, 1, blocklist);
    if (err) {
        printk("[ERROR] Failed to add register classes\n");
        return NULL;
    }

    sample_fix_code_triple(&base_usbd, USBD_SPEED_FS);
    usbd_self_powered(&base_usbd, attributes & USB_SCD_SELF_POWERED);

    // Register optional message callback for USB bus state events (suspend, resume, etc.)
    if (msg_cb != NULL) {
        err = usbd_msg_register_cb(&base_usbd, msg_cb);
        if (err) {
            printk("[ERROR] Failed to register message callback\n");
            return NULL;
        }
    }

    return &base_usbd;
}

// Runs the full initialisation sequence
struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb)
{
    // Configure descriptors, speed configs, and classes before finalising
    if (usbd_setup_device(msg_cb) == NULL) {
        return NULL;
    }

    // Finalise and enable the USB device stack
    int err = usbd_init(&base_usbd);
    if (err) {
        printk("[ERROR] Failed to initialize device support\n");
        return NULL;
    }

    return &base_usbd;
}
