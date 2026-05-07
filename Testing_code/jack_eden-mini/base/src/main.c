/*
 * Copyright (c) 2026 Jack Cairns, Eden Mehr
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "base_gatt.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>

// Fatal warning message on startup
static const char *const warn_message =
    "Big RIP. Nothing works now, power cycle the chip to try again";

/* ========================================================================== */
/* ENTRY POINT                                                                */
/* Initialise BT then kick off step 1. Everything else is                     */
/* event driven from the BT stack callbacks.                                  */
/* ========================================================================== */
int main(void)
{
    printk("CSSE4011 Mini-Project Base Chip\r\n");

    int err = bt_enable(NULL);
    if (err) {
        printk("[ERROR] Bluetooth init failed (err %d)\n%s\r\n", err, warn_message);
        return (err);
    }

    printk("[INFO] Bluetooth initialized\r\n");

    err = start_scan(); /* STEP 1 for GATT process */
    if (err) {
        printk("Initial scan failed (err %d)\n%s\n", err, warn_message);
        return (err);
    }

    return (0);
}

/*

1. Mobile to sniff iBeacons - Prac 5 code - repurposed Jack's prac 5 code - DONE

2. Base/Mobile to form GATT connection -> central_hr example plus Sam's example - Jack - DONE

3. Mobile to send packets via NUS -> peripheral_nus - Jack - DONE

4. Need either Red/Black Tree or a LinkedList containing all the 13 iBeacons  - Eden - DONE

5. Shell commands to add, remove, view iBeacons in the storage structure. - Eden - DONE

6. base also needs to be able to do what mobile does, minus the NUS coms stuff.  - Jack - DONE

7. Impliment an algorithm for calculating the approximate position of mobile - Jack - DONE

8. Impliment a Kalman filter to improve the approximate position algorithm. - Eden - DONE

9. Use Zephyr API for Json packet creation and send this data to the PC over UART - Jack - DONE

10. Modify existing GUI to accept these Json packets - Jack - DONE

11. Replace the shell interface with buttons in the GUI. - Jack - DONE

12. Setup webserver dashboard and api - Eden - DONE

13. Modify the GUI to send the data to a webserver. - DONE


DONE :)

*/
