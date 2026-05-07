/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_shell.h"
#include "json.h"
#include "observer.h"
#include "base_gatt.h"
#include "rb_tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/errno.h>

#include <zephyr/bluetooth/bluetooth.h>

/* ========================================================================== */
/* Compile-time Dispatch / Print Abstraction                                  */
/* CMD_ARGS, CMD_PRINT, CMD_WARN, CMD_ERR expand differently depending on     */
/* whether UART_USB_C is defined. In UART mode the shell handle is dropped    */
/* and output goes via printk. In shell mode the Zephyr shell API is used.    */
/* ========================================================================== */

#ifdef UART_USB_C
#define CMD_ARGS            size_t argc, char **argv
#define CMD_PRINT(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define CMD_WARN(fmt, ...)  printk("[WARN] " fmt "\n", ##__VA_ARGS__)
#define CMD_ERR(fmt, ...)   printk("[ERR] " fmt "\n", ##__VA_ARGS__)
#else
#include <zephyr/shell/shell.h>
#define CMD_ARGS            const struct shell *sh, size_t argc, char **argv
#define CMD_PRINT(fmt, ...) shell_print(sh, fmt "\n", ##__VA_ARGS__)
#define CMD_WARN(fmt, ...)  shell_warn(sh, fmt "\n", ##__VA_ARGS__)
#define CMD_ERR(fmt, ...)   shell_error(sh, fmt "\n", ##__VA_ARGS__)
#endif

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

typedef int (*shell_fn_t)(CMD_ARGS);

struct cmd_entry {
    const char *cmd;
    const char *sub;
    shell_fn_t handler;
};

static int cmd_listen_ble_start(CMD_ARGS);
static int cmd_listen_ble_stop(CMD_ARGS);
static int cmd_iBeacon_insert(CMD_ARGS);
static int cmd_iBeacon_remove(CMD_ARGS);
static int cmd_iBeacon_view(CMD_ARGS);

/* ========================================================================== */
/* Control State                                                              */
/* ========================================================================== */

static bool isListening = false;
static bool treeInitialised = false;
atomic_t mode = ATOMIC_INIT(0);

/* ========================================================================== */
/* Dispatch Table                                                             */
/* Maps (cmd, sub) string pairs to handler functions. Used by shell_dispatch  */
/* to route decoded JSON commands from the UART thread to the correct handler.*/
/* ========================================================================== */

/* clang-format off */
// Shell command dispatch table
static const struct cmd_entry shell_dispatch_table[] = {
    {"ble",         "start",    cmd_listen_ble_start},
    {"ble",         "stop",     cmd_listen_ble_stop},
    {"iBeacon",     "view",     cmd_iBeacon_view},
    {"iBeacon",     "add",      cmd_iBeacon_insert},
    {"iBeacon",     "delete",   cmd_iBeacon_remove}
};
/* clang-format on */

/* ========================================================================== */
/* BLE Listen Commands                                                        */
/* ========================================================================== */

/* Listen Start command */
static int cmd_listen_ble_start(CMD_ARGS)
{
    if (isListening) {
        CMD_WARN("Already listening");
        return (-EALREADY);
    }

    int err = close_connection();
    if (err < 0) {
        CMD_WARN("Gatt close failed, Not listening yet, try again");
        return (err);
    }

    // Might be scaning from boot, stop that here.
    bt_le_scan_stop();

    err = observer_start();
    if (err < 0) {
        CMD_WARN("Failed to start listening");
        return (err);
    }

    isListening = true;
    atomic_set(&mode, LISTEN_MODE);
    k_msgq_purge(&ibeacon_msg_queue);
    return (0);
}

/* Listen Stop command */
static int cmd_listen_ble_stop(CMD_ARGS)
{
    if (!isListening) {
        CMD_WARN("Not currently listening");
        return (-EALREADY);
    }

    int ret = observer_stop();
    if (ret < 0) {
        CMD_WARN("Failed to stop listening");
        return (ret);
    }

    isListening = false;
    atomic_set(&mode, STANDARD_MODE);
    k_msgq_purge(&ibeacon_msg_queue);
    start_scan();
    return (0);
}

/* ========================================================================== */
/* iBeacon Tree Commands                                                      */
/* ========================================================================== */

/* Insert into the rbTree a new iBeacon Node */
static int cmd_iBeacon_insert(CMD_ARGS)
{
    if (!treeInitialised) {
        init_rb_tree();
        treeInitialised = true;
    }

    char *name = argv[1];
    char *left = argv[8];
    char *right = argv[9];
    int err = 0;
    uint8_t mac[6];
    uint16_t major, minor;
    double x, y;
    int8_t cali;

    char *end;
    for (uint8_t i = 0; i < 6; i++) {
        mac[i] = (uint8_t)strtol(argv[2], &end, 16);
        if (end == argv[2] || (i < 5 && *end != ':')) {
            // Nothing happened | mising a ':'
            err = (-EINVAL);
            goto invalid_param;
        }

        // Skip ':'
        argv[2] = end + 1;
    }

    major = (uint16_t)strtol(argv[3], &end, 16);
    if (end == argv[3] || *end != '\0' || major == 0) {
        goto invalid_param;
    }

    minor = (uint16_t)strtol(argv[4], &end, 16);
    if (end == argv[4] || *end != '\0' || minor == 0) {
        goto invalid_param;
    }

    x = strtod(argv[5], &end);
    if (*end != '\0' || x < 0) {
        goto invalid_param;
    }

    y = strtod(argv[6], &end);
    if (*end != '\0' || y < 0) {
        goto invalid_param;
    }

    cali = (int8_t)strtol(argv[7], &end, 10);
    if (end == argv[7] || *end != '\0') {
        goto invalid_param;
    }

    CMD_PRINT("Valid params entered, attempting to add");

    err = insert_rb_node(name, mac, major, minor, x, y, cali, left, right);
    if (err < 0) {
        goto fail_insert;
    }

    return (0);

invalid_param:
    CMD_WARN("Invalid params entered");
fail_insert:
    CMD_WARN("Node insert failed, err %d", err);

    return (err);
}

/* Remove from the rbTree an iBeacon Node */
static int cmd_iBeacon_remove(CMD_ARGS)
{
    if (!treeInitialised) {
        init_rb_tree();
        treeInitialised = true;
    }

    char *end;
    char *name = argv[1];
    uint16_t major;
    uint16_t minor;

    major = (uint16_t)strtol(argv[2], &end, 16);
    if (end == argv[2] || *end != '\0' || major == 0) {
        goto invalid_param;
    }

    minor = (uint16_t)strtol(argv[3], &end, 16);
    if (end == argv[3] || *end != '\0' || major == 0) {
        goto invalid_param;
    }

    int err = remove_rb_node(name, major, minor);
    if (err < 0) {
        CMD_WARN("Node didn't exist");
        return (err);
    }

    return 0;

invalid_param:
    CMD_WARN("Invalid params entered");
    return (-EINVAL);
}

/* View an iBeacon or all Nodes from the rbTree */
static int cmd_iBeacon_view(CMD_ARGS)
{
    if (!treeInitialised) {
        init_rb_tree();
        treeInitialised = true;
    }

    char *opt = argv[1];

    if (!strcmp(opt, "-a")) {
        print_rb_node(NULL, true);
    } else {
        print_rb_node(opt, false);
    }

    return (0);
}

/* ========================================================================== */
/* UART Dispatch                                                              */
/* Called by the UART thread after JSON decode. Walks the dispatch table and  */
/* calls the matching handler with the reconstructed argc/argv.               */
/* ========================================================================== */

int shell_dispatch(size_t argc, char **argv, struct cmd_json *cmd)
{
    for (int i = 0; i < ARRAY_SIZE(shell_dispatch_table); i++) {
        if (strcmp(shell_dispatch_table[i].cmd, cmd->cmd) == 0 &&
            strcmp(shell_dispatch_table[i].sub, cmd->sub) == 0) {

            shell_dispatch_table[i].handler(argc, argv);
            return (0);
        }
    }

    // Command not Found
    return (-ESRCH);
}

/* ========================================================================== */
/* Zephyr Shell Registration                                                  */
/* Only compiled in when UART_USB_C is not defined.                           */
/* ========================================================================== */

/* clang-format off */
#ifndef UART_USB_C
SHELL_STATIC_SUBCMD_SET_CREATE(
    listen,
    SHELL_CMD_ARG(start, NULL,
                "Start listening\n"
                "Usage: ble listen start",
                cmd_listen_ble_start, 1, 0),
    SHELL_CMD_ARG(stop, NULL,
                "Stop listening\n"
                "Usage: ble listen stop",
                cmd_listen_ble_stop, 1, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(
    ble_cmds,
    SHELL_CMD(listen, &listen, "Listen commands", NULL),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(
    iBeacon_cmds,
    SHELL_CMD_ARG(add, NULL,
            "Add an iBeacon Node\n"
            "Name lengths longer than 9 are truncated\n"
            "Usage: iBeacon add <name> <mac> <major> <minor> <x> <y> <cali> <left> <right>",
            cmd_iBeacon_insert, 10, 0),
    SHELL_CMD_ARG(delete, NULL,
            "Remove an iBeacon Node\n"
            "Usage: iBeacon delete <name> <major> <minor>",
            cmd_iBeacon_remove, 4, 0),
    SHELL_CMD_ARG(view, NULL,
            "View an iBeacon Node or -a to view all nodes\n"
            "Usage: iBeacon view <-a | name>",
            cmd_iBeacon_view, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ble, &ble_cmds, "ble Commands", NULL);
SHELL_CMD_REGISTER(iBeacon, &iBeacon_cmds, "iBeacon Commands", NULL);
#endif
/* clang-format on */
