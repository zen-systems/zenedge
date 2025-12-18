#ifndef _DRIVERS_IVSHMEM_H
#define _DRIVERS_IVSHMEM_H

#include <stdint.h>

/* Using QEMU 'edu' device as proxy for IVSHMEM in this environment */
#define IVSHMEM_VENDOR_ID 0x1234
#define IVSHMEM_DEVICE_ID 0x11e8

/* Returns the virtual address of the shared memory, or 0 if not found */
void *ivshmem_get_shared_memory(void);

/* Detects and initializes the driver */
void ivshmem_init(void);

/* Returns the IRQ number (ISA IRQ, 0-15) assigned to the device */
uint8_t ivshmem_get_irq(void);

#endif /* _DRIVERS_IVSHMEM_H */
