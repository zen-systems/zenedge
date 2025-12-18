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
#include "../arch/pit.h"
#include "../include/string.h"
#include "sched_core.h"

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
process_t *process_list = NULL;
process_t *current_process = NULL;
static uint32_t next_pid = 1;

/* External Switch Function */
extern void switch_to(process_t *curr, process_t *next);

void schedule(void) {
    if (!current_process) return;

    /* Decrement quantum */
    if (current_process->ticks_remaining > 0) {
        current_process->ticks_remaining--;
        return;
    }

    /* Reset quantum */
    current_process->ticks_remaining = 5; // 50ms

    /* Round Robin: Find next READY process */
    process_t *next = current_process->next;
    
    // Safety check for empty list
    if (!next) return;

    // Iterate to find RUNNABLE/READY
    // For MVP, simplistic: just take next. 
    // Ideally check state == READY/RUNNING.
    
    if (next == current_process) return; /* Only one task */

    process_t *prev = current_process;
    current_process = next;

    // State transition
    if (prev->state == PROCESS_STATE_RUNNING) {
        prev->state = PROCESS_STATE_READY;
    }
    current_process->state = PROCESS_STATE_RUNNING;

    switch_to(prev, next);
}

/* 
 * MVP Test: Create 2 processes yielding to each other 
 */
void sched_test_rr(void) {
    console_write("[sched] initializing Process Model MVP...\n");

    /* 1. Create Idle/Kernel Process (PID 0) */
    paddr_t p_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
    process_t *idle = (process_t *)(phys_to_virt(p_phys));
    memset(idle, 0, sizeof(process_t));
    idle->pid = 0;
    idle->state = PROCESS_STATE_RUNNING;
    idle->ticks_remaining = 5;
    
    // Circular list
    idle->next = idle;
    process_list = idle;
    current_process = idle;
    
    console_write("[sched] Idle process created. Now creating User Process...\n");

    /* 2. Create User Process (Infinite Loop with Syscalls) */
    /* Allocate and Map Code Page manually for shellcode */
    paddr_t code_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
    vaddr_t user_code = 0x40000000;
    
    /* Write Shellcode to Physical Page */
    uint8_t *code_ptr = (uint8_t *)phys_to_virt(code_phys);
    
    /* Shellcode:
     * loop:
     *   mov eax, 1 (log)
     *   mov ebx, msg
     *   int 0x80
     *   mov eax, 2 (yield)
     *   int 0x80
     *   jmp loop
     */
    const char *msg = "Hello from Process 1!";
    uint8_t *msg_ptr = code_ptr + 0x100;
    strcpy((char*)msg_ptr, msg);
    
    int i = 0;
    /* mov eax, 1 */
    code_ptr[i++] = 0xB8; code_ptr[i++] = 0x01; code_ptr[i++] = 0x00; code_ptr[i++] = 0x00; code_ptr[i++] = 0x00;
    /* mov ebx, 0x40000100 */
    code_ptr[i++] = 0xBB; code_ptr[i++] = 0x00; code_ptr[i++] = 0x01; code_ptr[i++] = 0x00; code_ptr[i++] = 0x40;
    /* int 0x80 */
    code_ptr[i++] = 0xCD; code_ptr[i++] = 0x80;
    
    /* mov eax, 2 (yield) */
    code_ptr[i++] = 0xB8; code_ptr[i++] = 0x02; code_ptr[i++] = 0x00; code_ptr[i++] = 0x00; code_ptr[i++] = 0x00;
    /* int 0x80 */
    code_ptr[i++] = 0xCD; code_ptr[i++] = 0x80;
    
    /* jmp -21 (loop) */
    code_ptr[i++] = 0xEB; code_ptr[i++] = 0xEB;
    
    /* Create Process */
    process_t *proc1 = sched_create_user_process(user_code, 0, 0);
    
    if (proc1) {
        /* Map the code page into the new process's PD */
        paddr_t current_pd = vmm_get_current_pd();
        vmm_switch_pd(proc1->cr3);
        
        vmm_map_page(user_code, code_phys, PTE_USER_RW);
        // Stack was mapped in sched_create_user_process
        
        vmm_switch_pd(current_pd);
        
        /* Add to List */
        proc1->next = process_list->next;
        process_list->next = proc1;
        proc1->state = PROCESS_STATE_READY;
    }
}
