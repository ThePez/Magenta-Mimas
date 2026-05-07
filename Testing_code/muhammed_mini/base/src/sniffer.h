/* ============================================================== */
/* Sniffer Mode Header                                            */
/* Written: Muhammed A                                            */
/* ============================================================== */

#ifndef BASE_SNIFFER_H
#define BASE_SNIFFER_H

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>

extern atomic_t sniffer_active;

bool sniffer_write(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                       struct net_buf_simple *ad);

#endif
