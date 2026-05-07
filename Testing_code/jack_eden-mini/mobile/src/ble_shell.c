/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

////////////////////////////////////////////////////////////////////////////////
//   THIS FILE WON'T BE NEEDED ONCE ITS UP AND RUNNING                        //
////////////////////////////////////////////////////////////////////////////////

#include "observer.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/errno.h>

#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>

// Control Variables
static bool isListening = true;

/* ========================================================================== */
/* Shell Commands                                                             */
/* ========================================================================== */

/* Listen Start command */
static int cmd_listen_ble_start(const struct shell *sh, size_t argc, char **argv)
{
    if (isListening) {
        shell_warn(sh, "Already listening");
        return -EALREADY;
    }

    int ret = observer_start();
    if (ret == 0) {
        isListening = true;
    }
    return ret;
}

/* Listen Stop command */
static int cmd_listen_ble_stop(const struct shell *sh, size_t argc, char **argv)
{
    if (!isListening) {
        shell_warn(sh, "Not currently listening");
        return -EALREADY;
    }

    int ret = observer_stop();
    if (ret == 0) {
        isListening = false;
    }

    return ret;
}

/* ========================================================================== */
/* Shell Initialisation                                                       */
/* ========================================================================== */

/* clang-format off */
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

SHELL_CMD_REGISTER(ble, &ble_cmds, "ble Commands", NULL);
/* clang-format on */
