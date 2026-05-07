/* ============================================================== */
/* Multilateration calculator Header                              */
/* Written: Muhammed A                                            */
/* ============================================================== */

#ifndef BASE_LOCATION_H
#define BASE_LOCATION_H

#include "beacons.h"
#include "nus.h"

#include <stdlib.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>

#define LOCATION_UPDATE_RATE_MS 100

/* ========================================================================== */
/* Current Location                                                           */
/* ========================================================================== */

struct lateration {
    // Stored as two variables for JSON - in cm.
    int x;
    int y;
};

static const struct json_obj_descr lateration_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct lateration, x, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct lateration, y, JSON_TOK_NUMBER)};

/* ========================================================================== */
/* Function calls                                                             */
/* ========================================================================== */

int initialise_multilateration();

#endif
