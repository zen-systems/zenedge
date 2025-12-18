/* kernel/process.h - Process Control Block (PCB) definition */
#ifndef ZENEDGE_PROCESS_H
#define ZENEDGE_PROCESS_H

#include "mm/pmm.h"
#include <stdint.h>

/* Process states */
/* Process states */
typedef enum {
  PROCESS_STATE_NEW,
  PROCESS_STATE_READY,
  PROCESS_STATE_RUNNING,
  PROCESS_STATE_BLOCKED,
  PROCESS_STATE_ZOMBIE
} process_state_t;

/* Trapframe (matches syscall/interrupt stack layout) */
typedef struct trapframe {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, esp, ss;
} trapframe_t;

/* Process Control Block (PCB) */
/* IMPORTANT: First 4 fields MUST match switch.s offsets! */
typedef struct process {
  uint32_t pid;           /* +0 */
  uint32_t esp;           /* +4  (Saved by switch_to) */
  uint32_t cr3;           /* +8  (Physical PD address) */
  uint32_t kstack_top;    /* +12 (For TSS esp0) */
  
  /* --- End of Assembly Contract --- */
  
  process_state_t state;
  process_state_t prev_state; /* For debugging */

  /* Memory Context */
  uint32_t *pd_virt;      /* Virtual address of PD (for kernel access) */
  
  /* User Context */
  uint32_t ustack_top;
  trapframe_t *tf;        /* Setup on syscall/interrupt */

  /* Scheduling */
  uint32_t ticks_remaining;
  uint32_t quantum_ms;

  /* Contract / Resource Tracking */
  uint32_t mem_pages_used;
  uint32_t mem_pages_limit;
  
  /* WASM Agent Blob (if applicable) */
  const uint8_t* wasm_blob;
  uint32_t wasm_size;

  struct process *next;   /* Linked list */
} process_t;

#endif /* ZENEDGE_PROCESS_H */
