/* ========================================================================== */
/* Multilateration Calculator Source                                          */
/* Written: Muhammed A                                                        */
/* ========================================================================== */

// Foreword:
// Since our testing found that the device actually performs *significantly*
// worse when all 13 beacons are used for multilateration, we decided to skip
// that and just use the 4 best functioning beacons. The reason we went with
// 4 rather than 3 is because 3 beacons can cause zero-determinant situations
// if the 3 closest beacons are in a straight line. Therefore we actually
// wouldn't be able to tell on which side of the line we're on. This is much
// less of a problem with 4 beacons.
//
// Essentially, the more beacons you have, the harder it is for the system to
// not know which side you're on - but at the same time, due to the ridiculous
// amount of noise coming from the RSSI readings, the effect is just too much
// to handle (even with heavy Kalmanning)

#include "beacons.h"
#include "kalman.h"
#include "location.h"
#include "nus.h"
#include "sniffer.h"

#include <math.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>

/* ========================================================================== */
/* Memory management                                                          */
/* ========================================================================== */

// Max room dimensions
#define X_MAX 170
#define Y_MAX 450

#define THREAD_PRIORITY 5
#define THREAD_STACK    4096
#define HEAP_SIZE       4096

// We use a heap here since 4096 is not that much space when everything is
// 4-bit ints/doubles!
K_HEAP_DEFINE(location_heap, HEAP_SIZE);

/* ========================================================================== */
/* Current state                                                              */
/* ========================================================================== */

// Only accessed when distance_mutex is set.
static double pseudoinverse[2][MULTILATERATION_BEACONS - 1];

// These only get accessed from the one thread, so it doesn't need a mutex.
static struct lateration current_position;
static struct lateration current_position_kf;
static struct kalman kf_x;
static struct kalman kf_y;

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define JSON_BUFFER_SIZE_LOC 128
#define NODE_DELIMITER       "|"
#define VAL_DELIMITER        ","

/* ========================================================================== */
/* A-matrix calculators                                                       */
/* ========================================================================== */

// Why doesn't C have exponents?!
#define SQUARE(x) ((x) * (x))

// Calculate the pseudoinverse for the 4 closest beacons
static void recalculate_pseudoinverse(struct iBeacon_data **beacons)
{
    // Initialise some memory for the A matrix - this will be freed after
    int row_count = MULTILATERATION_BEACONS - 1;
    int16_t a_matrix[row_count][2];

    double a = 0, bc = 0, d = 0;

    for (int i = 0; i < row_count; i++) {
        a_matrix[i][0] = 2 * (beacons[0]->info->x - beacons[i + 1]->info->x);
        a_matrix[i][1] = 2 * (beacons[0]->info->y - beacons[i + 1]->info->y);

        // Simplified version - A^T * A actually comes out to:
        // A being the sum of all of the x-differences^2
        // D being the sum of all of the y-differences^2
        // B = C being the sum of all of the x*y differences
        a += SQUARE(a_matrix[i][0]);
        d += SQUARE(a_matrix[i][1]);
        bc += a_matrix[i][0] * a_matrix[i][1];
    }

    double determinant = (a * d) - SQUARE(bc);
    if (determinant == 0) {
        // Just don't bother...
        return;
    }

    // Get the inverse - so, (A^T A)^-1
    double temp = a;
    a = d / determinant;
    d = temp / determinant;
    bc = -bc / determinant;

    // Multiply by A^T*W one last time...
    for (int i = 0; i < row_count; i++) {
        pseudoinverse[0][i] = (a * a_matrix[i][0] + bc * a_matrix[i][1]);
        pseudoinverse[1][i] = (bc * a_matrix[i][0] + d * a_matrix[i][1]);
    }
}

// qsort helper to compare by distance of beacons
int distance_compare(const void *a, const void *b)
{
    const struct iBeacon_data *beaconA = *(const struct iBeacon_data **)a;
    const struct iBeacon_data *beaconB = *(const struct iBeacon_data **)b;

    return beaconA->distance - beaconB->distance;
}

// Sort the beacons by distance and return the array of pointers (not actual
// structs !!) on the heap. This needs to be freed!
static struct iBeacon_data **get_sorted_beacons(int count)
{
    struct iBeacon_data **beacons = k_heap_calloc(
        &location_heap, count, sizeof(struct iBeacon_data *), K_NO_WAIT);

    if (!beacons) {
        printk("L: Failed to make space for beacons array\n");
        return NULL;
    }

    struct iBeacon_data *list_node =
        SYS_SLIST_PEEK_HEAD_CONTAINER(&slist_iBeacon, list_node, node);

    for (int i = 0; i < count && list_node != NULL; i++) {
        beacons[i] = list_node;
        list_node = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node, node);
    }

    qsort(beacons, count, sizeof(struct iBeacon_data *), distance_compare);
    return beacons;
}

/* ========================================================================== */
/* Location calculator from b-matrix                                          */
/* ========================================================================== */

// Calculate location using x = (AtA)^-1AtB - this function just multiplies the
// stuff out the front with B.
static int calculate_location(double *x_meas, double *y_meas)
{
    // Don't change distances while this is running
    if (k_mutex_lock(&beacons_mutex, K_MSEC(LOCATION_UPDATE_RATE_MS)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return -1;
    }

    struct iBeacon_data **beacons =
        get_sorted_beacons(sys_slist_len(&slist_iBeacon));

    if (!beacons) {
        return -1;
    }

    recalculate_pseudoinverse(beacons);

    // Below is just the matrix calculations to find the B vector,
    int row_count = MULTILATERATION_BEACONS - 1;
    int32_t b_vec[row_count];
    int32_t k_term = SQUARE(beacons[0]->info->x) + SQUARE(beacons[0]->info->y) -
                     SQUARE(beacons[0]->distance);

    for (int i = 0; i < row_count; i++) {
        b_vec[i] = k_term - SQUARE(beacons[i + 1]->info->y) -
                   SQUARE(beacons[i + 1]->info->x) +
                   SQUARE(beacons[i + 1]->distance);
    }

    double x = 0, y = 0;

    // And then multiply by the stuff out the front
    for (int i = 0; i < row_count; i++) {
        x += b_vec[i] * pseudoinverse[0][i];
        y += b_vec[i] * pseudoinverse[1][i];
    }

    // Below just caps the location to the maximum (otherwise we get very
    // silly values)
    int sign = (y > 0) ? 1 : -1;

    if (abs(x) > X_MAX) {
        sign = (x > 0) ? 1 : -1;
        x = sign * X_MAX;
    }

    if (abs(y) > Y_MAX) {
        sign = (y > 0) ? 1 : -1;
        y = sign * Y_MAX;
    }

    *x_meas = x;
    *y_meas = y;
    k_heap_free(&location_heap, beacons);

    k_mutex_unlock(&beacons_mutex);

    return 0;
}

/* ========================================================================== */
/* Parser for received beacon data                                            */
/* ========================================================================== */

#define MAJOR_IDX 0
#define MINOR_IDX 1
#define RSSI_IDX  2

// Go through the linked list and update its distance using the formula
// desribed in class
static void check_list_and_update(int32_t major, int32_t minor, int32_t rssi)
{
    int cur_beacon = sys_slist_len(&slist_iBeacon);

    struct iBeacon_data *list_node =
        SYS_SLIST_PEEK_HEAD_CONTAINER(&slist_iBeacon, list_node, node);

    for (int i = 0; i < cur_beacon && list_node != NULL; i++) {
        if (list_node->info->major == major &&
            list_node->info->minor == minor) {
            double distance = (BEACONS_RSSI_A - rssi) / (10.0 * BEACONS_RSSI_N);

            // Get the distance in cm, rather than metres.
            distance = pow(10.0, distance) * 100;
            list_node->distance = distance;
            return;
        }
        list_node = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node, node);
    }
}

// Given the below (major,minor,rssi) packet, split it up on the commas, and
// then try to parse that and add it to the given nod.
static void parse_beacon_data(char *beacon_data)
{
    char *val_saveptr;
    char *endptr;
    char *val = strtok_r(beacon_data, VAL_DELIMITER, &val_saveptr);

    int32_t values[3] = {0, 0, 0};
    uint8_t value_count = 0;

    while (val != NULL && value_count < 3) {
        values[value_count++] = strtol(val, &endptr, 10);

        if (endptr == val) {
            return;
        }

        // Kind of ugly but it works.
        val = strtok_r(NULL, VAL_DELIMITER, &val_saveptr);
    }

    if (val) {
        return;
    }

    check_list_and_update(values[MAJOR_IDX], values[MINOR_IDX],
                          values[RSSI_IDX]);
}

// The data is of the form {major,minor,rssi}|... - this function just gets
// the individual three value packets, and sends them off to parse_beacon_data
// to be further split up on the commas.
static void parse_nus_data(char *data)
{
    char *beacon_saveptr;
    char *beacon_meas = strtok_r(data, NODE_DELIMITER, &beacon_saveptr);

    // Make sure that the beacons can't be removed/altered during this process
    if (k_mutex_lock(&beacons_mutex, K_MSEC(LOCATION_UPDATE_RATE_MS)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return;
    }

    while (beacon_meas != NULL) {
        parse_beacon_data(beacon_meas);
        beacon_meas = strtok_r(NULL, NODE_DELIMITER, &beacon_saveptr);
    }

    k_mutex_unlock(&beacons_mutex);
}

/* ========================================================================== */
/* Threads to receive data and calculate distance                             */
/* ========================================================================== */

// Get packets from the mobile node, try to parse them. This is a lifelong
// thread.
static void nus_collector(void *, void *, void *)
{
    while (1) {
        struct nus_package *package = k_fifo_get(&nus_package_fifo, K_FOREVER);

        if (package != NULL) {
            parse_nus_data(package->data);
            k_heap_free(&nus_package_heap, package->data);
            k_heap_free(&nus_package_heap, package);
        }

        k_msleep(5);
    }
}

// Calculate the location on a loop...
static void calculate_location_loop(void *, void *, void *)
{
    // Kalmans for both x and y
    kf_init(&kf_x, KF_Q, KF_R, KF_X0);
    kf_init(&kf_y, KF_Q, KF_R, KF_X0);

    char *json_buffer =
        k_heap_alloc(&location_heap, JSON_BUFFER_SIZE_LOC, K_NO_WAIT);

    // Make sure we aren't running out of memory!
    if (!json_buffer) {
        printk("L: OOPS! Not enough space for the json buffer.\n");
        return;
    }

    double x = 0, y = 0;

    while (1) {
        // predict the new locations...
        kf_predict(&kf_x);
        kf_predict(&kf_y);

        calculate_location(&x, &y);

        // Then set the current and kalmaned positions. We do velocity/distance
        // calculations on the computer since it wasn't specified to be done
        // here.
        current_position.x = x;
        current_position.y = y;

        current_position_kf.x = kf_x.x_k[0];
        current_position_kf.y = kf_y.x_k[0];

        // If the sniffer is active, we don't print out distance data. This
        // loop still keeps going though - there's no real reason to stop it
        // since it's still receiving data from NUS (This helps the kalman stay
        // accurate too).
        if (!atomic_get(&sniffer_active)) {
            json_buffer[0] = '\0';

            if (json_obj_encode_buf(lateration_descr,
                                    ARRAY_SIZE(lateration_descr),
                                    &current_position, json_buffer,
                                    JSON_BUFFER_SIZE_LOC) == 0) {
                // P prefix for non-kalman position
                printk("P: %s\n", json_buffer);
            }

            json_buffer[0] = '\0';

            if (json_obj_encode_buf(lateration_descr,
                                    ARRAY_SIZE(lateration_descr),
                                    &current_position_kf, json_buffer,
                                    JSON_BUFFER_SIZE_LOC) == 0) {
                // K prefix for kalman position
                printk("K: %s\n", json_buffer);
            }
        }

        // Finally, update your location in the Kalman filter!
        kf_update(&kf_x, x);
        kf_update(&kf_y, y);

        k_msleep(LOCATION_UPDATE_RATE_MS);
    }

    k_heap_free(&location_heap, json_buffer);
}

/* ========================================================================== */
/* Thread definitions                                                         */
/* ========================================================================== */

K_THREAD_STACK_DEFINE(calculate_location_stack, THREAD_STACK);
K_THREAD_STACK_DEFINE(nus_collector_stack, THREAD_STACK);

struct k_thread calculate_location_thread;
struct k_thread nus_collector_thread;

/* ========================================================================== */
/* Multilateration memory & task initialisers                                 */
/* ========================================================================== */

int initialise_multilateration(void)
{
    int ret = initialise_beacons();

    if (!ret) {
        // success...
        k_thread_create(&calculate_location_thread, calculate_location_stack,
                        K_THREAD_STACK_SIZEOF(calculate_location_stack),
                        calculate_location_loop, NULL, NULL, NULL,
                        THREAD_PRIORITY, 0, K_NO_WAIT);
        k_thread_create(&nus_collector_thread, nus_collector_stack,
                        K_THREAD_STACK_SIZEOF(nus_collector_stack),
                        nus_collector, NULL, NULL, NULL, THREAD_PRIORITY, 0,
                        K_NO_WAIT);
        return 0;
    }

    return -1;
}
