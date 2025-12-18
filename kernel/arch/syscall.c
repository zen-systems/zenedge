/* kernel/arch/syscall.c - System Call Handler */

#include "syscall.h"
#include "../console.h"
#include "../process.h"          /* For process_exit, etc. */
#include "../sched/sched_core.h" /* For schedule/yield */
#include "idt.h"

/*
 * Syscall Numbers (ABI)
 * 0: sys_exit
 * 1: sys_log
 * 2: sys_yield
 */

/* Forward declarations */
static void sys_exit(int status);
static void sys_log(const char *msg);
static void sys_yield(void);

/* Main Syscall Handler
 * Called from isr_handler when int_no == 128
 */
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

  default:
    console_write("[syscall] unknown syscall: ");
    // print_uint(syscall_num); needed, but let's skip for now
    console_write("\n");
    break;
  }

  /* TODO: Set return value in frame->eax if needed */
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
  /* TODO: Implement process termination */
  /* For now, just hang or yield forever */
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
