/* ========================================================================== */
/* iBeacons Source                                                            */
/* Written: Muhammed A                                                        */
/* ========================================================================== */

#include "beacons.h"

#include <zephyr/kernel.h>

/* ========================================================================== */
/* Memory management                                                          */
/* ========================================================================== */

#define MUTEX_TIMEOUT_MS 100
#define JSON_BUFFER_SIZE 256
#define HEAP_SIZE        8192

// Only slist_iBeacon needs the mutex
sys_slist_t slist_iBeacon;
sys_slist_t slist_iBeacon_arbitrary;

K_MUTEX_DEFINE(beacons_mutex);

K_HEAP_DEFINE(beacons_heap, HEAP_SIZE);

/* ========================================================================== */
/* Default beacon setup                                                       */
/* ========================================================================== */

static void free_iBeacon_info(struct iBeacon_info *info)
{
    k_heap_free(&beacons_heap, info->name);
    k_heap_free(&beacons_heap, info->address);
    k_heap_free(&beacons_heap, info->left_name);
    k_heap_free(&beacons_heap, info->right_name);
    k_heap_free(&beacons_heap, info);
}

static void free_iBeacon_data(struct iBeacon_data *beacon)
{
    free_iBeacon_info(beacon->info);
    k_heap_free(&beacons_heap, beacon);
}

// This is used at the beginning of the program to make a beacon and copy over
// the initial data defined in beacons.h
static bool make_default_beacon(int index)
{
    // Type should be between 0 and MAX_BEACON_COUNT
    struct iBeacon_data *data =
        k_heap_alloc(&beacons_heap, sizeof(struct iBeacon_data), K_NO_WAIT);

    if (data == NULL) {
        printk("L: Error allocating memory!\n");
        return false;
    }

    data->info =
        k_heap_alloc(&beacons_heap, sizeof(struct iBeacon_info), K_NO_WAIT);

    if (!(data->info)) {
        k_heap_free(&beacons_heap, data);
        printk("L: Error allocating memory!\n");
        return false;
    }

    memset(&data->node, 0, sizeof(sys_snode_t));

    data->info->major = majors_init[index];
    data->info->minor = minors_init[index];
    data->info->x = xs_init[index];
    data->info->y = ys_init[index];

    data->distance = 0;

    data->info->address =
        k_heap_alloc(&beacons_heap, ADDRESS_BUFFER_LEN, K_NO_WAIT);
    data->info->name =
        k_heap_alloc(&beacons_heap, NAME_INITIAL_BUFFER_LEN, K_NO_WAIT);
    data->info->left_name =
        k_heap_alloc(&beacons_heap, NAME_INITIAL_BUFFER_LEN, K_NO_WAIT);
    data->info->right_name =
        k_heap_alloc(&beacons_heap, NAME_INITIAL_BUFFER_LEN, K_NO_WAIT);

    if (!data->info->name && !data->info->left_name &&
        !data->info->right_name && !data->info->address) {
        free_iBeacon_data(data);
        printk("L: Failed to allocate memory\n");
        return false;
    }

    strcpy(data->info->address, addresses_init[index]);
    strcpy(data->info->name, name_init);
    strcpy(data->info->left_name, name_init);
    strcpy(data->info->right_name, name_init);

    // Change the letter of the left and right names (simple circular)
    data->info->name[5] += index;
    data->info->right_name[5] +=
        ((MAX_BEACON_COUNT + index + 1) % MAX_BEACON_COUNT);
    data->info->left_name[5] +=
        ((MAX_BEACON_COUNT + index - 1) % MAX_BEACON_COUNT);

    sys_slist_append(&slist_iBeacon, &(data->node));

    return true;
}

int initialise_beacons()
{
    if (k_mutex_lock(&beacons_mutex, K_MSEC(100)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return -1;
    }

    // Initialise singly linked list containing iBeacon
    sys_slist_init(&slist_iBeacon);
    sys_slist_init(&slist_iBeacon_arbitrary);

    for (int i = 0; i < MAX_BEACON_COUNT; i++) {
        if (!make_default_beacon(i)) {
            k_mutex_unlock(&beacons_mutex);
            return -1;
        }
    }

    k_mutex_unlock(&beacons_mutex);
    return 0;
}

// Add a beacon out of the 13 given its character 
int add_beacon(char name)
{
    if (k_mutex_lock(&beacons_mutex, K_MSEC(MUTEX_TIMEOUT_MS)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return -1;
    }

    int cur_beacon = sys_slist_len(&slist_iBeacon);

    struct iBeacon_data *list_node =
        SYS_SLIST_PEEK_HEAD_CONTAINER(&slist_iBeacon, list_node, node);

    if (!list_node) {
        k_mutex_unlock(&beacons_mutex);
        printk("L: Beacons do not exist\n");
        return -1;
    }

    for (int i = 0; i < cur_beacon && list_node != NULL; i++) {
        // Check its letter
        if (list_node->info->name[5] == name) {
            k_mutex_unlock(&beacons_mutex);
            printk("L: Beacon already added!\n");
            return -1;
        }

        list_node = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node, node);
    }

    int res = make_default_beacon(name - 'A');

    k_mutex_unlock(&beacons_mutex);

    if (!res) {
        return -2;
    }

    return 0;
}

// The struct here is supplied from the shell command - that's where the 
// inputs are validated as well. Otherwise it is very similar to how the 
// initial node is validated (except here it is iBeacon_info instead of 
// iBeacon_data)
int add_arbitrary_beacon(struct iBeacon_info *info_args)
{
    // No mutex needed for this one
    if (sys_slist_len(&slist_iBeacon_arbitrary) >= MAX_ARB_COUNT) {
        printk("L: Reached maximum limit of arbitrary beacons!\n");
        return -1;
    }

    struct iBeacon_info *info =
        k_heap_alloc(&beacons_heap, sizeof(struct iBeacon_info), K_NO_WAIT);

    if (!info_args) {
        printk("L: Error allocating memory!\n");
        return -1;
    }

    memset(&info->node, 0, sizeof(sys_snode_t));
    info->major = info_args->major;
    info->minor = info_args->minor;
    info->x = info_args->x;
    info->y = info_args->y;

    info->address = k_heap_alloc(&beacons_heap, ADDRESS_BUFFER_LEN, K_NO_WAIT);
    info->name =
        k_heap_alloc(&beacons_heap, strlen(info_args->name) + 1, K_NO_WAIT);
    info->left_name = k_heap_alloc(&beacons_heap,
                                   strlen(info_args->left_name) + 1, K_NO_WAIT);
    info->right_name = k_heap_alloc(
        &beacons_heap, strlen(info_args->right_name) + 1, K_NO_WAIT);

    if (!info->name && !info->left_name && !info->right_name &&
        !info->address) {
        free_iBeacon_info(info);
        printk("L: Failed to allocate memory\n");
        return -1;
    }

    strcpy(info->address, info_args->address);
    strcpy(info->name, info_args->name);
    strcpy(info->right_name, info_args->right_name);
    strcpy(info->left_name, info_args->left_name);

    sys_slist_append(&slist_iBeacon_arbitrary, &(info->node));

    return 0;
}

// Remove one of the 13 beacons given its character. We impose a limit of 
// 4 beacons minimum for reasons described in location.c
int remove_beacon(char name)
{
    if (k_mutex_lock(&beacons_mutex, K_MSEC(MUTEX_TIMEOUT_MS)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return -1;
    }

    int cur_beacon = sys_slist_len(&slist_iBeacon);

    if (cur_beacon == MULTILATERATION_BEACONS) {
        printk("L: Cannot have less than %d beacons!\n",
               MULTILATERATION_BEACONS);
        k_mutex_unlock(&beacons_mutex);
        return -1;
    }

    struct iBeacon_data *prev = NULL;
    struct iBeacon_data *list_node =
        SYS_SLIST_PEEK_HEAD_CONTAINER(&slist_iBeacon, list_node, node);

    for (int i = 0; i < cur_beacon && list_node != NULL; i++) {
        if (list_node->info->name[5] == name) {
            // You have to give it the previous node to delete, so it's 
            // important to make sure that you're not trying to reference a
            // value within a NULL
            sys_slist_remove(&slist_iBeacon,
                             (prev == NULL) ? NULL : &(prev->node),
                             &(list_node->node));
            free_iBeacon_data(list_node);
            k_mutex_unlock(&beacons_mutex);
            return 0;
        }

        prev = list_node;
        list_node = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node, node);
    }

    k_mutex_unlock(&beacons_mutex);

    printk("L: Beacon already removed\n");
    return -1;
}

// Same as above, except checking addresses.
int remove_arbitrary_beacon(char *address)
{
    int cur_arb = sys_slist_len(&slist_iBeacon_arbitrary);

    if (cur_arb == 0) {
        printk("L: There are no arbitrary beacons!\n");
        return -1;
    }

    struct iBeacon_info *prev = NULL;
    struct iBeacon_info *list_node = SYS_SLIST_PEEK_HEAD_CONTAINER(
        &slist_iBeacon_arbitrary, list_node, node);

    for (int i = 0; i < cur_arb && list_node != NULL; i++) {
        if (strcmp(list_node->address, address) == 0) {
            sys_slist_remove(&slist_iBeacon_arbitrary,
                             (prev == NULL) ? NULL : &(prev->node),
                             &(list_node->node));
            free_iBeacon_info(list_node);
            return 0;
        }

        prev = list_node;
        list_node = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node, node);
    }

    printk("L: Arbitary beacon not added\n");
    return -1;
}

// List both types of beacons - the 13 multilaterative ones as well as the
// non laterative ones. These are sent out with an I/A prefix depending on 
// whether it's an (I)Beacon or an (A)rbitrary bluetooth node.
int list_beacons()
{
    if (k_mutex_lock(&beacons_mutex, K_MSEC(MUTEX_TIMEOUT_MS)) != 0) {
        printk("L: Mutex not unlocking!\n");
        return -1;
    }

    int cur_beacon = sys_slist_len(&slist_iBeacon);
    // No stack overflows here!
    char *json_buffer =
        k_heap_alloc(&beacons_heap, JSON_BUFFER_SIZE, K_NO_WAIT);

    if (json_buffer == NULL) {
        k_mutex_unlock(&beacons_mutex);
        printk("L: Could not free space for beacons!\n");
        return -1;
    }

    struct iBeacon_data *list_node_data =
        SYS_SLIST_PEEK_HEAD_CONTAINER(&slist_iBeacon, list_node_data, node);

    for (int i = 0; i < cur_beacon && list_node_data != NULL; i++) {
        // Send out the beacon data as a JSON as required
        if (json_obj_encode_buf(
                iBeacon_info_descr, ARRAY_SIZE(iBeacon_info_descr),
                list_node_data->info, json_buffer, JSON_BUFFER_SIZE) == 0) {
            printk("I: %s\n", json_buffer);
            k_msleep(1);
        }

        list_node_data = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node_data, node);
    }
 
    // We don't need the mutex to access the arbitary beacons (it's only 
    // accessed from the shell command thread)
    k_mutex_unlock(&beacons_mutex);

    int cur_arb = sys_slist_len(&slist_iBeacon_arbitrary);

    struct iBeacon_info *list_node_info = SYS_SLIST_PEEK_HEAD_CONTAINER(
        &slist_iBeacon_arbitrary, list_node_info, node);

    for (int i = 0; i < cur_arb && list_node_info != NULL; i++) {
        if (json_obj_encode_buf(iBeacon_info_descr,
                                ARRAY_SIZE(iBeacon_info_descr), list_node_info,
                                json_buffer, JSON_BUFFER_SIZE) == 0) {
            printk("A: %s\n", json_buffer);
            k_msleep(1);
        }

        list_node_info = SYS_SLIST_PEEK_NEXT_CONTAINER(list_node_info, node);
    }

    k_heap_free(&beacons_heap, json_buffer);
    return 0;
}
