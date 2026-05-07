/* ============================================================== */
/* NUS connection header                                          */
/* Written: Muhammed A                                            */
/* ============================================================== */

#ifndef BASE_NUS_H
#define BASE_NUS_H

#include <zephyr/kernel.h>

#define ADDRESS_BUFFER_LEN 18
#define NUS_MAX_MTU        247


// Making space for our data fifo - since the data pointer is so huge it has
// to be made on the heap - same with the struct itself (since it gets
// given to the FIFO and there is no guarantee the callingfunction will still 
// have its stack intact!)
struct nus_package {
    void *fifo_reserved;
    char *data;
};

extern struct k_heap nus_package_heap;
extern struct k_fifo nus_package_fifo;

int start_scan();

#endif
