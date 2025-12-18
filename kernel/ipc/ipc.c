/* kernel/ipc/ipc.c - IPC Producer/Consumer Implementation
 *
 * Implements the ZENEDGE side of the Proxy Driver protocol:
 * - Command ring: ZENEDGE produces, Linux consumes
 * - Response ring: Linux produces, ZENEDGE consumes
 * - Doorbell mechanism for interrupt signaling
 * - Shared heap for tensor/model data
 */

#include "ipc.h"
#include "../arch/idt.h"
#include "../arch/pic.h"
#include "../console.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../time/time.h"
#include "heap.h"

/* Shared Memory Base Address (Physical) */
#define IPC_SHARED_MEM_PHYS 0x02000000
#define IPC_SHARED_MEM_SIZE 0x100000 /* 1MB total */

/* IRQ for IPC response notifications (using IRQ 10, vector 42) */
#define IPC_IRQ 10

/* Exported for heap.c */
uint32_t ipc_shm_base = 0;

/* Ring and doorbell pointers */
/* Base address of the Shared Memory Region (Virtual) */
static void *ipc_shmem_base = NULL;
static volatile ipc_ring_t *cmd_ring = NULL;
static volatile ipc_rsp_ring_t *rsp_ring = NULL;
static volatile doorbell_ctl_t *doorbell = NULL;

/* Statistics */
static uint32_t irq_count = 0;

void ipc_init(void *base_addr, uint8_t irq) {
  console_write("[ipc] initializing proxy driver...\n");

  if (base_addr == NULL) {
    console_write("[ipc] Error: No shared memory base provided!\n");
    return;
  }

  ipc_shmem_base = base_addr;

  /* Initialize pointers based on Dynamic Base */
  /* Layout:
   * 0x00000: Command Ring (32KB)
   * 0x08000: Response Ring (32KB)
   * 0x10000: Doorbell Control (64KB)
   * 0x20000: Heap Control (4KB)
   * 0x21000: Heap Data Block 0 ...
   */

  uint8_t *base = (uint8_t *)ipc_shmem_base;

  cmd_ring = (ipc_ring_t *)(base + IPC_CMD_RING_OFFSET);
  rsp_ring = (ipc_rsp_ring_t *)(base + IPC_RSP_RING_OFFSET);
  doorbell = (doorbell_ctl_t *)(base + IPC_DOORBELL_OFFSET);

  /* Initial Setup (Producer Side) */
  cmd_ring->head = 0;
  cmd_ring->tail = 0;
  cmd_ring->size = IPC_RING_SIZE; // Assuming IPC_RING_SIZE is the correct macro
                                  // for RING_SIZE
  cmd_ring->magic =
      IPC_MAGIC; // Assuming IPC_MAGIC is the correct macro for RING_MAGIC

  /* Initialize Response Ring Header */
  rsp_ring->magic = IPC_RSP_MAGIC;
  rsp_ring->head = 0;
  rsp_ring->tail = 0;
  rsp_ring->size = IPC_RING_SIZE;

  console_write("[ipc] cmd ring at ");
  print_hex32((uint32_t)cmd_ring);
  console_write("\n");

  console_write("[ipc] rsp ring at ");
  print_hex32((uint32_t)rsp_ring);
  console_write("\n");

  console_write("[ipc] doorbell at ");
  print_hex32((uint32_t)doorbell);
  console_write("\n");

  /* Initialize Doorbell Control Block */
  doorbell->magic = IPC_DOORBELL_MAGIC;
  doorbell->version = 1;
  doorbell->cmd_doorbell = 0;
  doorbell->cmd_flags = 0;
  doorbell->cmd_irq_count = 0;
  doorbell->rsp_doorbell = 0;
  doorbell->rsp_flags = DOORBELL_FLAG_IRQ_ENABLED; /* Enable response IRQs */
  doorbell->rsp_irq_count = 0;
  doorbell->cmd_writes = 0;
  doorbell->rsp_writes = 0;

  /* Print actual locations */
  console_write("[ipc] cmd ring at ");
  print_hex32((uint32_t)cmd_ring);
  console_write("\n[ipc] rsp ring at ");
  print_hex32((uint32_t)rsp_ring);
  console_write("\n[ipc] doorbell at ");
  print_hex32((uint32_t)doorbell);
  console_write("\n");

  /* Initialize Heap */
  heap_init((void *)(base + IPC_HEAP_CTL_OFFSET));

  /* Register Interrupt Handler */
  /* IRQ is the ISA IRQ number (e.g. 11) */
  /* IDT vector = IRQ_BASE (32) + irq */
  if (irq > 0 && irq < 16) {
    console_write("[ipc] registering IRQ handler on vector ");
    print_uint(32 + irq);
    console_write("\n");

    idt_register_handler(32 + irq, ipc_irq_handler);
    pic_unmask_irq(irq);
  } else {
    console_write("[ipc] Warning: Invalid IRQ, polling mode only.\n");
  }
}

/* Ring the command doorbell to notify Linux */
static void ring_cmd_doorbell(uint32_t head) {
  if (!doorbell)
    return;

  /* Memory barrier before doorbell write */
  __asm__ __volatile__("" ::: "memory");

  /* Write doorbell (signals Linux: "process up to this head") */
  doorbell->cmd_doorbell = head;
  doorbell->cmd_writes++;

  /* Set pending flag if IRQ enabled on Linux side */
  if (doorbell->cmd_flags & DOORBELL_FLAG_IRQ_ENABLED) {
    doorbell->cmd_flags |= DOORBELL_FLAG_PENDING;
    doorbell->cmd_irq_count++;
  }
}

/* =============================================================================
 * KERNEL MESH IMPLEMENTATION
 * =============================================================================
 */

/* Mesh state */
static volatile mesh_table_t *mesh_table = NULL;
static int local_node_id = -1;

void ipc_mesh_init(void) {
    if (!ipc_shmem_base) return;
    
    mesh_table = (mesh_table_t*)((uint8_t*)ipc_shmem_base + IPC_MESH_OFFSET);
    
    /* Initialize table if magic is missing (Race condition? First wins) */
    if (mesh_table->magic != MESH_MAGIC) {
        mesh_table->magic = MESH_MAGIC;
        mesh_table->version = 1;
        mesh_table->active_nodes = 0;
        for(int i=0; i<MES_MAX_NODES; i++) {
            mesh_table->nodes[i].status = NODE_STATUS_OFFLINE;
        }
        console_write("[ipc] initialized mesh table\n");
    }
    
    /* Register Local Node */
    for(int i=0; i<MES_MAX_NODES; i++) {
         if (mesh_table->nodes[i].status == NODE_STATUS_OFFLINE) {
             /* Claim it */
             mesh_table->nodes[i].status = NODE_STATUS_ALIVE;
             mesh_table->nodes[i].node_id = i;
             mesh_table->nodes[i].cpu_load = 0;
             mesh_table->nodes[i].heartbeat = 0;
             
             local_node_id = i;
             mesh_table->active_nodes++; // Not atomic but okay for demo
             
             console_write("[ipc] joined mesh as Node ");
             print_uint(local_node_id);
             console_write("\n");
             break;
         }
    }
    
    if (local_node_id == -1) {
        console_write("[ipc] WARNING: mesh full, could not join\n");
    }
}

void ipc_mesh_update(void) {
    if (local_node_id < 0 || !mesh_table) return;
    
    /* Update heartbeat */
    mesh_table->nodes[local_node_id].heartbeat++; 
}

void ipc_mesh_dump(void) {
    if (!mesh_table || mesh_table->magic != MESH_MAGIC) {
        console_write("[ipc] mesh not initialized\n");
        return;
    }
    
    console_write("=== KERNEL MESH ===\n");
    for(int i=0; i<MES_MAX_NODES; i++) {
        if (mesh_table->nodes[i].status != NODE_STATUS_OFFLINE) {
            console_write("Node ");
            print_uint(i);
            console_write(": ");
            if (i == local_node_id) console_write("(ME) ");
            console_write(mesh_table->nodes[i].status == NODE_STATUS_ALIVE ? "ALIVE" : "BUSY");
            console_write(" Load=");
            print_uint(mesh_table->nodes[i].cpu_load);
            console_write(" Heartbeat=");
            print_hex32((uint32_t)mesh_table->nodes[i].heartbeat);
            console_write("\n");
        }
    }
    console_write("===================\n");
}

int ipc_send(uint16_t cmd, uint32_t payload) {
  return ipc_send_flags(cmd, payload, 0);
}

int ipc_send_flags(uint16_t cmd, uint32_t payload, uint16_t flags) {
  if (!cmd_ring)
    return -1;

  uint32_t head = cmd_ring->head;
  uint32_t next_head = (head + 1) % cmd_ring->size;

  if (next_head == cmd_ring->tail) {
    console_write("[ipc] cmd ring full!\n");
    return -1; /* Full */
  }

  /* Write Packet */
  volatile ipc_packet_t *pkt = &cmd_ring->data[head];
  pkt->cmd = cmd;
  pkt->flags = flags;
  pkt->payload_id = payload;
  pkt->timestamp = time_usec();

  /* Compiler barrier before publishing */
  __asm__ __volatile__("" ::: "memory");

  cmd_ring->head = next_head;

  /* Ring doorbell to notify Linux */
  ring_cmd_doorbell(next_head);

  return 0;
}

int ipc_has_response(void) {
  if (!rsp_ring || rsp_ring->magic != IPC_RSP_MAGIC)
    return 0;
  return rsp_ring->head != rsp_ring->tail;
}

int ipc_poll_response(ipc_response_t *rsp) {
  if (!rsp_ring || rsp_ring->magic != IPC_RSP_MAGIC)
    return 0;

  uint32_t head = rsp_ring->head;
  uint32_t tail = rsp_ring->tail;

  if (head == tail)
    return 0; /* Empty */

  /* Read response */
  volatile ipc_response_t *resp = &rsp_ring->data[tail];

  if (rsp) {
    rsp->status = resp->status;
    rsp->orig_cmd = resp->orig_cmd;
    rsp->result = resp->result;
    rsp->timestamp = resp->timestamp;
  }

  /* Log response */
  console_write("[ipc] response: status=");
  print_hex32(resp->status);
  console_write(" cmd=");
  print_hex32(resp->orig_cmd);
  console_write(" result=");
  print_hex32(resp->result);
  console_write("\n");

  /* Compiler barrier before updating tail */
  __asm__ __volatile__("" ::: "memory");

  /* Update tail (consume) */
  rsp_ring->tail = (tail + 1) % rsp_ring->size;

  return 1;
}

/* Simulation of Linux Side (for testing without real bridge) */
void ipc_consume_one(void) {
  if (!cmd_ring)
    return;

  uint32_t head = cmd_ring->head;
  uint32_t tail = cmd_ring->tail;

  if (head == tail) {
    /* console_write("[ipc-sim] cmd ring empty.\n"); */
    return;
  }

  volatile ipc_packet_t *pkt = &cmd_ring->data[tail];
  console_write("[ipc-sim] Consuming Packet:\n");
  console_write("  Cmd: ");
  print_hex32(pkt->cmd);
  console_write("\n");
  console_write("  Payload: ");
  print_hex32(pkt->payload_id);
  console_write("\n");
  console_write("  Timestamp: ");
  print_uint((uint32_t)pkt->timestamp);
  console_write("us\n");

  /* Store command for response */
  uint16_t orig_cmd = pkt->cmd;

  /* Update tail (consume the command) */
  cmd_ring->tail = (tail + 1) % cmd_ring->size;

  /* Send a mock response */
  if (rsp_ring && rsp_ring->magic == IPC_RSP_MAGIC) {
    uint32_t rsp_head = rsp_ring->head;
    uint32_t next_rsp_head = (rsp_head + 1) % rsp_ring->size;

    if (next_rsp_head != rsp_ring->tail) {
      volatile ipc_response_t *resp = &rsp_ring->data[rsp_head];
      resp->status = RSP_OK;
      resp->orig_cmd = orig_cmd;
      resp->result = 0x12345678; /* Mock result */
      resp->timestamp = time_usec();

      __asm__ __volatile__("" ::: "memory");
      rsp_ring->head = next_rsp_head;

      /* Ring response doorbell (simulating Linux side) */
      if (doorbell) {
        doorbell->rsp_doorbell = next_rsp_head;
        doorbell->rsp_writes++;
        if (doorbell->rsp_flags & DOORBELL_FLAG_IRQ_ENABLED) {
          doorbell->rsp_flags |= DOORBELL_FLAG_PENDING;
          doorbell->rsp_irq_count++;
        }
      }

      console_write("[ipc-sim] Sent response (RSP_OK), rang doorbell\n");
    }
  }
}

/* Enable/disable response IRQs */
void ipc_enable_irq(int enable) {
  if (!doorbell)
    return;

  if (enable) {
    doorbell->rsp_flags |= DOORBELL_FLAG_IRQ_ENABLED;
    console_write("[ipc] Response IRQs enabled\n");
  } else {
    doorbell->rsp_flags &= ~DOORBELL_FLAG_IRQ_ENABLED;
    console_write("[ipc] Response IRQs disabled\n");
  }
}

/* IRQ handler for response notifications */
void ipc_irq_handler(interrupt_frame_t *frame) {
  (void)frame; /* Unused */
  irq_count++;

  /* Clear pending flag */
  if (doorbell) {
    doorbell->rsp_flags &= ~DOORBELL_FLAG_PENDING;
  }

  /* Process all pending responses */
  ipc_response_t rsp;
  while (ipc_poll_response(&rsp)) {
    /* Response was logged by ipc_poll_response */
  }
}

void ipc_dump_debug(void) {
  console_write("[ipc] === DEBUG DUMP ===\n");

  /* Command ring status */
  if (cmd_ring) {
    console_write("[ipc] CMD Ring:\n");
    console_write("  Magic: ");
    print_hex32(cmd_ring->magic);
    console_write(cmd_ring->magic == IPC_MAGIC ? " (valid)\n" : " (INVALID)\n");
    console_write("  Head:  ");
    print_uint(cmd_ring->head);
    console_write("\n  Tail:  ");
    print_uint(cmd_ring->tail);
    console_write("\n");

    uint32_t pending = (cmd_ring->head >= cmd_ring->tail)
                           ? (cmd_ring->head - cmd_ring->tail)
                           : (cmd_ring->size - cmd_ring->tail + cmd_ring->head);
    console_write("  Pending: ");
    print_uint(pending);
    console_write(" packets\n");
  }

  /* Response ring status */
  if (rsp_ring) {
    console_write("[ipc] RSP Ring:\n");
    console_write("  Magic: ");
    print_hex32(rsp_ring->magic);
    console_write(rsp_ring->magic == IPC_RSP_MAGIC ? " (valid)\n"
                                                   : " (INVALID)\n");
    console_write("  Head:  ");
    print_uint(rsp_ring->head);
    console_write("\n  Tail:  ");
    print_uint(rsp_ring->tail);
    console_write("\n");

    uint32_t pending = (rsp_ring->head >= rsp_ring->tail)
                           ? (rsp_ring->head - rsp_ring->tail)
                           : (rsp_ring->size - rsp_ring->tail + rsp_ring->head);
    console_write("  Pending: ");
    print_uint(pending);
    console_write(" responses\n");
  }

  /* Doorbell status */
  if (doorbell) {
    console_write("[ipc] Doorbell:\n");
    console_write("  Magic: ");
    print_hex32(doorbell->magic);
    console_write(doorbell->magic == IPC_DOORBELL_MAGIC ? " (valid)\n"
                                                        : " (INVALID)\n");
    console_write("  CMD doorbell: ");
    print_uint(doorbell->cmd_doorbell);
    console_write(" (writes: ");
    print_uint(doorbell->cmd_writes);
    console_write(", irqs: ");
    print_uint(doorbell->cmd_irq_count);
    console_write(")\n");
    console_write("  RSP doorbell: ");
    print_uint(doorbell->rsp_doorbell);
    console_write(" (writes: ");
    print_uint(doorbell->rsp_writes);
    console_write(", irqs: ");
    print_uint(doorbell->rsp_irq_count);
    console_write(")\n");
    console_write("  RSP IRQ enabled: ");
    console_write((doorbell->rsp_flags & DOORBELL_FLAG_IRQ_ENABLED) ? "yes\n"
                                                                    : "no\n");
    console_write("  Local IRQ count: ");
    print_uint(irq_count);
    console_write("\n");
  }

  console_write("[ipc] === END DUMP ===\n");
}


