/* ============================================================== */
/* Main file - contains all the shell commands                    */
/* Written: Muhammed A                                            */
/* ============================================================== */

#include "location.h"
#include "nus.h"
#include "sniffer.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

/* ========================================================================== */
/* Scanning                                                                   */
/* ========================================================================== */

// Just a bunch of shell command handlers..
static int handle_sniff_change(const struct shell *sh, bool sniffing)
{
    shell_print(sh, "L: Sniffing mode set to: %s",
                sniffing ? "Enabled" : "Disabled");
    atomic_set(&sniffer_active, sniffing);
    return 0;
}

static int cmd_sniff_start(const struct shell *sh, size_t argc, char **argv)
{
    bool current_mode = atomic_get(&sniffer_active);
    if (current_mode) {
        shell_error(sh, "L: Already sniffing!");
        return -EINVAL;
    }

    return handle_sniff_change(sh, true);
}

static int cmd_sniff_stop(const struct shell *sh, size_t argc, char **argv)
{
    bool current_mode = atomic_get(&sniffer_active);
    if (!current_mode) {
        shell_error(sh, "L: Sniffing already stopped!");
        return -EINVAL;
    }

    return handle_sniff_change(sh, false);
}

static int cmd_sniff_toggle(const struct shell *sh, size_t argc, char **argv)
{
    return handle_sniff_change(sh, !atomic_get(&sniffer_active));
}

/* ========================================================================== */
/* Ibeacon Shell Commands                                                     */
/* ========================================================================== */

static int cmd_ibeacon_add(const struct shell *sh, size_t argc, char **argv)
{
    char *beacon = argv[1];

    if (!beacon) {
        shell_error(sh, "L: No item found!");
        return 1;
    }

    int beacon_index = beacon[0];

    if (beacon_index < 'A' || beacon_index >= 'N') {
        shell_error(sh, "L: Give a valid beacon!");
        return 1;
    }

    return add_beacon(beacon_index);
}

static int cmd_ibeacon_add_arb(const struct shell *sh, size_t argc, char **argv)
{
    const char* empty = "";
    struct iBeacon_info info;

    if (strlen(argv[1]) > MAX_NAME_LEN) {
        shell_error(sh, "L: Name must be 30 or less characters");
        return 1;
    }
    info.name = argv[1];

    // WE can use this function to check if the address given is in the wright
    // format, it will return false if not.
    bt_addr_t addr;
    if (bt_addr_from_str(argv[2], &addr)) {
        shell_error(sh, "L: Address is not well formed!");
        return 2;
    }
    info.address = argv[2];

    char* endptr;

    info.major = strtol(argv[3], &endptr, 10);
    if (endptr == argv[3] || info.major > 65535 || info.major < 0) {
        shell_error(sh, "L: Major must be a 16-bit number");
        return 3;
    }

    info.minor = strtol(argv[4], &endptr, 10);
    if (endptr == argv[4] || info.minor > 65535 || info.minor < 0) {
        shell_error(sh, "L: Minor must be a 16-bit number");
        return 3;
    }

    info.x = strtol(argv[5], &endptr, 10);
    if (endptr == argv[5]) {
        shell_error(sh, "L: X must be a number");
        return 3;
    }

    info.y = strtol(argv[6], &endptr, 10);
    if (endptr == argv[6]) {
        shell_error(sh, "L: Y must be a number");
        return 3;
    }

    // I hate data parsing...

    if (argc == 9) {
        if (strlen(argv[7]) > MAX_NAME_LEN || strlen(argv[8]) > MAX_NAME_LEN) {
            shell_error(sh, "L: Name must be 30 or less characters");
            return 1;
        }

        info.left_name = argv[7];
        info.right_name = argv[8];
    } else {
        info.left_name = (char*) empty;
        info.right_name = (char*) empty;
    }

    return add_arbitrary_beacon(&info);
}

static int cmd_ibeacon_remove(const struct shell *sh, size_t argc, char **argv)
{
    char *beacon = argv[1];

    if (!beacon) {
        shell_error(sh, "L: No item found!");
        return 1;
    }

    int beacon_index = beacon[0];

    if (beacon_index < 'A' || beacon_index >= 'N') {
        shell_error(sh, "L: Give a valid beacon!");
        return 1;
    }

    return remove_beacon(beacon_index);
}

static int cmd_ibeacon_remove_arb(const struct shell *sh, size_t argc,
                                  char **argv)
{
    char *address = argv[1];
    bt_addr_t addr;

    if (bt_addr_from_str(address, &addr)) {
        shell_error(sh, "L: Address is not well formed!");
        return -1;
    }

    return remove_arbitrary_beacon(address);
}

static int cmd_ibeacon_list(const struct shell *sh, size_t argc, char **argv)
{
    if (list_beacons()) {
        shell_error(sh, "L: Could not list beacons for some reason");
        return -1;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sniffer_cmds, SHELL_CMD(start, NULL, "Start sniffing", cmd_sniff_start),
    SHELL_CMD(stop, NULL, "Stop sniffing", cmd_sniff_stop),
    SHELL_CMD(toggle, NULL, "Toggle sniffing", cmd_sniff_toggle),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    ibeacon_cmds,
    SHELL_CMD_ARG(add, NULL,
                  "Add an iBeacon Node\n"
                  "Usage: ibeacon add <node>",
                  cmd_ibeacon_add, 2, 0),
    SHELL_CMD_ARG(add_arb, NULL,
                  "Add an arbitrary beacon\n"
                  "Usage: ibeacon add_arb <name> <address> <major> <minor> <x> "
                  "<y> [left_name] [right_name]",
                  cmd_ibeacon_add_arb, 7, 2),
    SHELL_CMD_ARG(remove, NULL,
                  "Remove an iBeacon Node\n"
                  "Usage: ibeacon remove <node>",
                  cmd_ibeacon_remove, 2, 0),
    SHELL_CMD_ARG(remove_arb, NULL,
                  "Remove an arbitary beacon\n"
                  "Usage: ibeacon remove <address>",
                  cmd_ibeacon_remove_arb, 2, 0),
    SHELL_CMD(list, NULL, "List all iBeacon Nodes", cmd_ibeacon_list),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sniffer, &sniffer_cmds, "Sniffer Commands", NULL);
SHELL_CMD_REGISTER(ibeacon, &ibeacon_cmds, "iBeacon Commands", NULL);

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    int err = bt_enable(NULL);
    if (err) {
        return err;
    }

    initialise_multilateration();
    start_scan();

    return 0;
}
