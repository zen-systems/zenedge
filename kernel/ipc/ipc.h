/* kernel/ipc/ipc.h */

#ifndef _IPC_H
#define _IPC_H

#include "../arch/idt.h" /* For interrupt_frame_t */
#include "ipc_proto.h"
#include <stdint.h>

/* Initialize IPC with a specific shared memory base address (virtual) and
 * IRQ vector */
/* If base_addr is NULL, it fails */
void ipc_init(void *base_addr, uint8_t irq);

/* Send a command to Linux bridge
 * Returns: 0 on success, -1 on failure (ring full)
 */
int ipc_send(uint16_t cmd, uint32_t payload);

/* Send a command with flags */
int ipc_send_flags(uint16_t cmd, uint32_t payload, uint16_t flags);

/* Poll for a response (returns 1 if consumed, 0 if empty) */
int ipc_poll_response(ipc_response_t *rsp);

/* Check if there is a pending response */
int ipc_has_response(void);

/* Enable/Disable IRQs (used by consumer) */
void ipc_enable_irq(int enable);

/* IRQ handler for IPC notifications */
void ipc_irq_handler(interrupt_frame_t *frame);

/* Simulation/testing: consume one command (mock Linux side) */
void ipc_consume_one(void);

/* Dump debug stats to console */
void ipc_dump_debug(void);

void ipc_mesh_init(void);
void ipc_mesh_update(void);
void ipc_mesh_dump(void);

#endif /* _IPC_H */
