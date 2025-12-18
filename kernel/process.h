/* kernel/process.h - Process Control Block (PCB) definition */
#ifndef ZENEDGE_PROCESS_H
#define ZENEDGE_PROCESS_H

#include "mm/pmm.h"
#include <stdint.h>

/* Process states */
typedef enum {
  PROCESS_STATE_READY,
  PROCESS_STATE_RUNNING,
  PROCESS_STATE_BLOCKED,
  PROCESS_STATE_ZOMBIE
} process_state_t;

/* Process Control Block (PCB) */
typedef struct process {
  uint32_t pid; /* Process ID */

  /* CPU Context */
  uint32_t esp; /* Kernel stack pointer (saved context) */

  /* Memory Context */
  paddr_t cr3; /* Page directory physical address */

  /* Kernel Stack Info */
  uint32_t kstack_top; /* Top of kernel stack (for TSS esp0) */

  process_state_t state;    /* Current state */
  uint32_t ticks_remaining; /* Time quantum */

  struct process *next; /* Linked list for simple scheduler */
} process_t;

#endif /* ZENEDGE_PROCESS_H */
