/* kernel/arch/syscall.c - System Call Handler */

#include "syscall.h"
#include "../console.h"
#include "../process.h"          /* For process_exit, etc. */
#include "../sched/sched_core.h" /* For schedule/yield */
#include "idt.h"
#include "../ipc/heap.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

/*
 * Syscall Numbers (ABI)
 * 0: sys_exit
 * 1: sys_log
 * 2: sys_yield
 * 3: sys_map_tensor
 */

/* Forward declarations */
static void sys_exit(int status);
static void sys_log(const char *msg);
static void sys_yield(void);
static uint32_t sys_map_tensor(uint16_t blob_id);

/* sys_map_tensor(blob_id) -> vaddr */
static uint32_t sys_map_tensor(uint16_t blob_id) {
    uint32_t phys_addr = heap_get_blob_phys(blob_id);
    if (!phys_addr) {
        console_write("[syscall] map_tensor: invalid blob\n");
        return 0;
    }
    
    uint32_t size = heap_get_blob_size(blob_id);
    /* Align size to pages */
    
    /* Find a free user vaddr. Simple bump allocator for now.
     * We'll assume user space starts at 0x80000000 and goes up.
     */
    static uint32_t next_map_addr = 0x80000000;
    
    uint32_t vaddr = next_map_addr;
    
    /* Map it! user=1, rw=1. Flags: PTE_USER (4) | PTE_RW (2) | PTE_PRESENT (1) = 7 */
    if (vmm_map_range(vaddr, phys_addr, size, 0x07) != 0) {
         console_write("[syscall] map_tensor: map failed\n");
         return 0;
    }
    
    /* Advance bump pointer (aligned to 4KB) */
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    next_map_addr += pages * PAGE_SIZE;
    
    console_write("[syscall] mapped blob ");
    print_uint(blob_id);
    console_write(" to ");
    print_hex32(vaddr);
    console_write("\n");
    
    return vaddr;
}

/* Main Syscall Handler */
void syscall_handler(interrupt_frame_t *frame) {
  uint32_t syscall_num = frame->eax;

  switch (syscall_num) {
  case 0: /* sys_exit */
    sys_exit((int)frame->ebx);
    break;

  case 1: /* sys_log */
    sys_log((const char *)frame->ebx);
    break;

  case 2: /* sys_yield */
    sys_yield();
    break;
    
  case 3: /* sys_map_tensor */
    frame->eax = sys_map_tensor((uint16_t)frame->ebx);
    break;

  default:
    console_write("[syscall] unknown syscall: ");
    // print_uint(syscall_num);
    console_write("\n");
    break;
  }
}

/* Initialize syscalls */
void syscall_init(void) {
  console_write("[syscall] registering syscall handler on vector 128\n");
  idt_register_handler(INT_SYSCALL, syscall_handler);
}

/* --- Implementations --- */

static void sys_exit(int status) {
  (void)status;
  console_write("[syscall] sys_exit called. Terminating process.\n");
  while (1) {
    schedule();
  }
}

static void sys_log(const char *msg) {
  /* TODO: Validate pointer is in user space */
  console_write("[USER] ");
  console_write(msg);
  console_write("\n");
}

static void sys_yield(void) { schedule(); }
