/* kernel/sched/sched_core.c */
#include "sched_core.h"
#include "../arch/gdt.h"
#include "../console.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../process.h"
#include "../time/time.h"
#include "../trace/flightrec.h"
#include "../ipc/ipc.h"
#include "../ipc/ipc_proto.h"
#include "../drivers/ivshmem.h"
#include "../arch/pit.h"

/* Helper to print uint in sched */
/* Now using global console helpers print_uint and print_hex32 */

static volatile int ipc_irq_received = 0;

static void sched_ipc_callback(void) {
    ipc_irq_received = 1;
}

static void execute_step(const job_step_t *s, const task_contract_t *c) {
  (void)c;

  /* For non-compute steps, simulation is fine for now */
  /* In a real system, COLLECTIVE would also use IPC/Fabric */
  if (s->type != STEP_TYPE_COMPUTE) {
      console_write("[sched] simulating non-compute step ");
      print_uint(s->id);
      console_write("\n");
      for (volatile uint32_t i = 0; i < 100000; i++) { }
      return;
  }

  console_write("[sched] Offloading COMPUTE step ");
  print_uint(s->id);
  console_write(" to Bridge...\n");

  /* Identify input tensor (use first input as payload) */
  uint32_t payload_id = 0;
  if (s->num_inputs > 0) {
      payload_id = s->inputs[0];
  }

  /* Send IPC Command */
  cycles_t start_cycles = rdtsc();
  
  if (ipc_send(CMD_RUN_MODEL, payload_id) != 0) {
      console_write("[sched] Failed to send IPC command (Ring full?)\n");
      return;
  }

  /* Poll for Completion (Adaptive Polling) */
  /* Strategy: Spin for 2ms (covering most fast inferences), then sleep. */
  
  ipc_response_t rsp;
  cycles_t wait_start = rdtsc();
  int timeout_ms = 5000;
  int received = 0;
  
  while (timeout_ms > 0) {
      if (ipc_poll_response(&rsp)) {
          received = 1;
          break;
      }
      
      /* Check how long we've been waiting in this cycle */
      usec_t elapsed = cycles_to_usec(rdtsc() - wait_start);
      
      /* Increase spin threshold to handle TSC calibration skew.
       * 100000us assumed might be only 25ms real time if CPU is fast.
       */
      if (elapsed < 100000) {
          /* Busy wait / Relax */
          __asm__ __volatile__("pause");
      } else {
          /* We've spun for 2ms, switch to sleep to save CPU */
          pit_sleep_ms(1);
          timeout_ms--; 
          /* Reset spin counter? No, just sleep from now on?
           * Actually, for a single step, if it takes >2ms, we can just sleep.
           * But to be robust, we just keep sleeping after the initial spin period expired.
           */
      }
  }

  if (received) {
      cycles_t end_cycles = rdtsc();
      usec_t total_rtt_us = cycles_to_usec(end_cycles - start_cycles);
      usec_t server_us = (usec_t)rsp.timestamp; /* Repurposed for duration */
      usec_t transport_us = (total_rtt_us > server_us) ? (total_rtt_us - server_us) : 0;

      console_write("[sched] Step complete. Result: ");
      print_hex32(rsp.result);
      console_write("\n");
      
      console_write("[sched] Latency Breakdown: Total=");
      print_uint((uint32_t)total_rtt_us);
      console_write("us (Server=");
      print_uint((uint32_t)server_us);
      console_write("us, Transport=");
      print_uint((uint32_t)transport_us);
      console_write("us)\n");

      /* Optional: Validation of result? */
      if (rsp.status != RSP_OK) {
           console_write("[sched] Remote error status: ");
           print_hex32(rsp.status);
           console_write("\n");
      }
  } else {
      console_write("[sched] TIMEOUT waiting for remote execution!\n");
  }
}

void sched_init(void) { console_write("[sched] init\n"); }

void sched_run_job(sched_job_ctx_t *ctx) {
  if (!ctx || !ctx->job)
    return;

  console_write("[sched] run_job begin (budget: ");
  print_uint(ctx->contract.cpu_budget_us);
  console_write("us)\n");

  /* Log job submission */
  flightrec_log(TRACE_EVT_JOB_SUBMIT, ctx->job->id, 0, ctx->job->num_steps);

  /* Super simple: while there is a ready step, "run" it. */
  while (1) {
    int sid = job_graph_next_ready(ctx->job);
    if (sid < 0) {
      console_write("[sched] no ready steps left\n");
      break;
    }

    /* find the step struct */
    job_step_t *step = NULL;
    for (uint8_t i = 0; i < ctx->job->num_steps; i++) {
      if (ctx->job->steps[i].id == (step_id_t)sid) {
        step = &ctx->job->steps[i];
        break;
      }
    }
    if (!step) {
      console_write("[sched] step lookup failure\n");
      break;
    }

    /* Begin span - this logs STEP_START and tracks start time */
    trace_span_t span =
        flightrec_begin_span(TRACE_EVT_STEP_START, ctx->job->id, (uint32_t)sid);

    execute_step(step, &ctx->contract);

    /* End span - this logs STEP_END with duration in 'extra' field */
    flightrec_end_span(span, TRACE_EVT_STEP_END);

    /* Check contract: did this step exceed per-step budget? */
    usec_t step_duration = flightrec_last_duration(ctx->job->id, (uint32_t)sid);
    usec_t per_step_budget = ctx->contract.cpu_budget_us / ctx->job->num_steps;

    if (step_duration > per_step_budget) {
      /* Budget exceeded - log violation */
      console_write("[sched] BUDGET EXCEED: step ");
      print_uint((uint32_t)sid);
      console_write(" took ");
      print_uint((uint32_t)step_duration);
      console_write("us (limit: ");
      print_uint((uint32_t)per_step_budget);
      console_write("us)\n");

      flightrec_log(TRACE_EVT_CONTRACT_BUDGET_EXCEED, ctx->job->id,
                    (uint32_t)sid, (uint32_t)step_duration);
    } else if (step_duration > (per_step_budget * 80) / 100) {
      /* Approaching budget (>80%) - log warning */
      flightrec_log(TRACE_EVT_CONTRACT_BUDGET_WARN, ctx->job->id, (uint32_t)sid,
                    (uint32_t)step_duration);
    }

    job_graph_mark_completed(ctx->job, (step_id_t)sid);
  }

  /* Log job completion and get stats */
  flightrec_log(TRACE_EVT_JOB_COMPLETE, ctx->job->id, 0, 0);

  trace_job_stats_t stats;
  flightrec_get_job_stats(ctx->job->id, &stats);

  console_write("[sched] run_job end - ");
  print_uint(stats.steps_completed);
  console_write(" steps, ");
  print_uint((uint32_t)stats.total_cpu_usec);
  console_write("us CPU, ");
  print_uint(stats.violations);
  console_write(" violations\n");
}

/* Scheduler Data */
static process_t *process_list = NULL;
static process_t *current_process = NULL;
static uint32_t next_pid = 1;

/* External Switch Function */
extern void switch_to(process_t *curr, process_t *next);
extern void enter_user_mode(void *entry_point, void *user_stack);

/* Create a new process */
process_t *sched_create_process(void (*entry_point)(void)) {
  /* 1. Allocate Process Struct */
  paddr_t p_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
  if (!p_phys)
    return NULL;

  /* Using the physical page as kernel memory - primitive but works for now */
  process_t *proc = (process_t *)(phys_to_virt(p_phys));

  proc->pid = next_pid++;
  proc->ticks_remaining = 5; /* 50ms quantum */
  proc->state = PROCESS_STATE_READY;

  /* 2. Create Page Directory */
  proc->cr3 = vmm_create_user_pd();
  if (!proc->cr3) {
    pmm_free_page(p_phys);
    return NULL;
  }

  /* 3. Allocate Kernel Stack */
  /* We allocate 1 page for kernel stack */
  paddr_t kstack_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
  proc->kstack_top = phys_to_virt(kstack_phys) + 4096;

  /*
   * 4. Set up Stack for Context Switch
   * We need to forge a stack that looks like 'switch_to' just saved it.
   * switch_to pops: EDI, ESI, EBX, EBP, then RET.
   */

  /* Simplified: We assume 'entry_point' IS the kernel wrapper if needed,
   * OR we treat this as a kernel thread for now.
   *
   * Let's make 'sched_create_user_process' specifically.
   */

  return proc;
}

process_t *sched_create_user_process(vaddr_t entry, vaddr_t stack_top) {
  process_t *proc = sched_create_process(NULL);
  if (!proc)
    return NULL;

  /*
   * For user process, the "kernel thread" that starts it needs to:
   * 1. Be running on the new kernel stack.
   * 2. Call enter_user_mode(entry, stack_top).
   *
   * We forge the kernel stack to look like:
   * [ ... ]
   * [RET ADDR ] -> enter_user_mode
   * [ARG 2    ] -> stack_top
   * [ARG 1    ] -> entry
   * [Garbage  ] -> (saved EBP, EBX, ESI, EDI)
   */

  uint32_t *sp = (uint32_t *)proc->kstack_top;

  /* Arguments for enter_user_mode (pushed right-to-left if called, but here we
   * setup stack frame) */
  /* When 'switch_to' returns, it does 'ret'. ESP points to return address.
   * Arguments are at ESP+4, ESP+8.
   */

  *(--sp) = stack_top; /* Arg 2 */
  *(--sp) = entry;     /* Arg 1 */
  *(--sp) =
      0; /* Fake Return Address (so enter_user_mode sees args at +4, +8) */
  *(--sp) = (uint32_t)enter_user_mode; /* Return Address for switch_to */

  /* Callee-saved regs popped by switch_to */
  *(--sp) = 0; /* EBP */
  *(--sp) = 0; /* EBX */
  *(--sp) = 0; /* ESI */
  *(--sp) = 0; /* EDI */

  proc->esp = (uint32_t)sp;

  /* Add to list */
  if (!process_list) {
    process_list = proc;
    proc->next = proc; /* Circular */
  } else {
    proc->next = process_list->next;
    process_list->next = proc;
  }

  return proc;
}

void schedule(void) {
  if (!current_process)
    return;

  /* Decrement quantum */
  if (current_process->ticks_remaining > 0) {
    current_process->ticks_remaining--;
    return;
  }

  /* Reset quantum */
  current_process->ticks_remaining = 5;

  /* Pick next */
  process_t *next = current_process->next;

  if (next == current_process)
    return; /* Only one task */

  /* Switch */
  process_t *prev = current_process;
  current_process = next;

  switch_to(prev, next);
}

/* Temporary: Init scheduler with a dummy kernel process (idle) and a user
 * process */
void sched_test_rr(void) {
  console_write("[sched] initializing RR scheduler...\n");

  /* 1. Create access for current execution (Idle Task) */
  paddr_t p_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
  process_t *idle = (process_t *)(phys_to_virt(p_phys));
  idle->pid = 0;
  idle->state = PROCESS_STATE_RUNNING;
  idle->ticks_remaining = 5;
  /* We don't know the exact stack top or CR3, but switch_to will overwrite them
   * into 'idle' structure. CR3 is read from register. ESP is read from
   * register.
   */
  idle->next = idle;

  process_list = idle;
  current_process = idle;

  /* 2. Create User Process (Infinite Loop A) */
  /* Allocate code page */
  paddr_t code_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
  vaddr_t user_code = 0x40000000;
  /* We need to temporarily map it to copy data, but for simplicity:
   * We map it in the *current* PD, copy data, and then when we create user PD,
   * we rely on vmm_create_user_pd to NOT copy it, so we must map it manually
   * there.
   *
   * actually let's map it in current kernel (identity) to write, then map in
   * user.
   */

  /* 4. Copy "shellcode" to user memory
   * We want to do:
   *   while(1) {
   *     sys_log("Hello User!"); // eax=1, ebx=msg
   *     sys_yield();            // eax=2
   *   }
   *
   * Assembly:
   * start:
   *   mov $1, %eax      ; sys_log
   *   mov $0x40000100, %ebx ; msg address (offset 0x100 into page)
   *   int $0x80
   *   mov $2, %eax      ; sys_yield
   *   int $0x80
   *   jmp start
   *
   * Machine Code:
   *   B8 01 00 00 00       mov eax, 1
   *   BB 00 01 00 40       mov ebx, 0x40000100
   *   CD 80                int 0x80
   *   B8 02 00 00 00       mov eax, 2
   *   CD 80                int 0x80
   *   EB EB                jmp start (-21 bytes? need to calculate jump)
   */
  /* 4. Copy "shellcode" to user memory
   * We want to do:
   *   while(1) {
   *     sys_log("Hello User!"); // eax=1, ebx=msg
   *     sys_yield();            // eax=2
   *   }
   */
  uint8_t *code_ptr = (uint8_t *)phys_to_virt(code_phys);

  /* msg at 0x40000100 */
  const char *msg = "Hello from User Space!";
  uint8_t *msg_ptr = code_ptr + 0x100;
  for (int i = 0; msg[i]; i++)
    msg_ptr[i] = msg[i];
  msg_ptr[22] = 0;

  int i = 0;
  code_ptr[i++] = 0xB8;
  code_ptr[i++] = 0x01;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00; /* mov eax, 1 */
  code_ptr[i++] = 0xBB;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x01;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x40; /* mov ebx, 0x40000100 */
  code_ptr[i++] = 0xCD;
  code_ptr[i++] = 0x80; /* int 0x80 */

  /* Log */
  code_ptr[i++] = 0xB8;
  code_ptr[i++] = 0x01;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00; /* mov eax, 1 */
  code_ptr[i++] = 0xBB;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x01;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x40; /* mov ebx, msg */
  code_ptr[i++] = 0xCD;
  code_ptr[i++] = 0x80; /* int 0x80 */

  /* Yield */
  code_ptr[i++] = 0xB8;
  code_ptr[i++] = 0x02;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00;
  code_ptr[i++] = 0x00; /* mov eax, 2 */
  code_ptr[i++] = 0xCD;
  code_ptr[i++] = 0x80; /* int 0x80 */

  /* Loop: jmp start (-21) */
  code_ptr[i++] = 0xEB;
  code_ptr[i++] = 0xEB; /* -21 = 0xEB */

  /* Clear any trailing bytes if needed (not needed as we jump) */

  /* Create process */
  /* Note: sched_create_user_process creates a PD. We must map execution pages
   * into it. */
  process_t *proc = sched_create_user_process(user_code, 0xB0000000);

  /* Map code and stack in the NEW process's PD */
  paddr_t old_pd = vmm_get_current_pd();
  vmm_switch_pd(proc->cr3);

  vmm_map_page(user_code, code_phys, PTE_USER_RW); // Code
  paddr_t stack_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
  vmm_map_page(0xB0000000 - 0x1000, stack_phys, PTE_USER_RW); // Stack

  vmm_switch_pd(old_pd);
}
