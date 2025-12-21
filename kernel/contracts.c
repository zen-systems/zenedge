/* kernel/contracts.c
 *
 * Contract-based resource governance implementation
 *
 * Tracks CPU and memory usage against budgets, transitions state
 * machine on violations, and logs all events to flight recorder.
 *
 * Uses the portable zenedge_alloc abstraction for memory allocation,
 * enabling this module to be ported to other RTOSes without changes.
 */
#include "contracts.h"
#include "console.h"
#include "trace/flightrec.h"
#include "zenedge_alloc.h"

void contracts_init(void) { console_write("[contracts] init\n"); }

const char *contract_state_name(contract_state_t state) {
  switch (state) {
  case CONTRACT_STATE_OK:
    return "OK";
  case CONTRACT_STATE_WARNED:
    return "WARNED";
  case CONTRACT_STATE_SAFE_MODE:
    return "SAFE_MODE";
  default:
    return "UNKNOWN";
  }
}

void contract_apply(task_contract_t *c) {
  console_write("[contracts] applying contract for job ");
  print_uint(c->job_id);
  console_write("\n");

  /* Initialize runtime accounting */
  c->cpu_used_us = 0;
  c->mem_used_kb = 0;
  c->cpu_violations = 0;
  c->mem_violations = 0;
  c->state = CONTRACT_STATE_OK;

  contract_register(c);

  /* Set preferred node based on priority */
  if (c->prio == CONTRACT_PRIORITY_REALTIME) {
    c->preferred_node = NUMA_NODE_LOCAL; /* Node 0 for latency */
  } else {
    c->preferred_node = NUMA_NODE_REMOTE; /* Node 1 for background */
  }

  console_write("[contracts] priority=");
  switch (c->prio) {
  case CONTRACT_PRIORITY_LOW:
    console_write("LOW");
    break;
  case CONTRACT_PRIORITY_NORMAL:
    console_write("NORMAL");
    break;
  case CONTRACT_PRIORITY_HIGH:
    console_write("HIGH");
    break;
  case CONTRACT_PRIORITY_REALTIME:
    console_write("REALTIME");
    break;
  }
  console_write(", node=");
  print_uint(c->preferred_node);
  console_write(", cpu_budget=");
  print_uint(c->cpu_budget_us);
  console_write("us, mem_budget=");
  print_uint(c->memory_kb);
  console_write("KB\n");

  /* Log contract application */
  flightrec_log(TRACE_EVT_CONTRACT_APPLY, c->job_id, 0, c->cpu_budget_us);
}

int contract_charge_cpu(task_contract_t *c, uint32_t usec) {
  c->cpu_used_us += usec;

  if (c->cpu_used_us > c->cpu_budget_us) {
    c->cpu_violations++;

    /* Log violation */
    flightrec_log(TRACE_EVT_CONTRACT_BUDGET_EXCEED, c->job_id, 0,
                  c->cpu_used_us);

    /* State machine transition */
    if (c->state == CONTRACT_STATE_OK) {
      contract_set_state(c, CONTRACT_STATE_WARNED);
    } else if (c->state == CONTRACT_STATE_WARNED && c->cpu_violations >= 3) {
      contract_set_state(c, CONTRACT_STATE_SAFE_MODE);
    }

    return 1; /* Violation */
  }

  return 0;
}

int contract_charge_memory(task_contract_t *c, uint32_t kb) {
  c->mem_used_kb += kb;

  if (c->mem_used_kb > c->memory_kb) {
    c->mem_violations++;

    /* Log memory violation */
    flightrec_log(TRACE_EVT_MEM_CONTRACT_EXCEED, c->job_id, 0, c->mem_used_kb);

    /* State machine transition */
    if (c->state == CONTRACT_STATE_OK) {
      contract_set_state(c, CONTRACT_STATE_WARNED);
    } else if (c->state == CONTRACT_STATE_WARNED && c->mem_violations >= 2) {
      contract_set_state(c, CONTRACT_STATE_SAFE_MODE);
    }

    return 1; /* Violation */
  }

  return 0;
}

paddr_t contract_alloc_page(task_contract_t *c) {
  /* Check if we're in safe mode */
  if (c->state == CONTRACT_STATE_SAFE_MODE) {
    console_write("[contracts] allocation denied: SAFE_MODE\n");
    flightrec_log(TRACE_EVT_MEM_ALLOC_FAIL, c->job_id, 0, 0);
    return 0;
  }

  /* Pre-check: Will this allocation exceed budget? */
  uint32_t page_kb = PAGE_SIZE / 1024;
  if (c->mem_used_kb + page_kb > c->memory_kb) {
    c->mem_violations++;

    console_write("[contracts] allocation denied: BUDGET EXCEEDED\n");

    /* Log memory violation */
    flightrec_log(TRACE_EVT_MEM_CONTRACT_EXCEED, c->job_id, 0,
                  c->mem_used_kb + page_kb);

    /* State machine transition */
    if (c->state == CONTRACT_STATE_OK) {
      contract_set_state(c, CONTRACT_STATE_WARNED);
    } else if (c->state == CONTRACT_STATE_WARNED && c->mem_violations >= 2) {
      contract_set_state(c, CONTRACT_STATE_SAFE_MODE);
    }

    return 0; /* Deny */
  }

  /* Map preferred node to portable representation */
  uint8_t node_pref;
  switch (c->preferred_node) {
  case NUMA_NODE_LOCAL:
    node_pref = ZNODE_LOCAL;
    break;
  case NUMA_NODE_REMOTE:
    node_pref = ZNODE_REMOTE;
    break;
  default:
    node_pref = ZNODE_ANY;
    break;
  }

  /* Allocate using portable abstraction */
  zalloc_result_t result = zenedge_alloc_page(node_pref);

  if (result.addr) {
    /* Commit charge */
    c->mem_used_kb += page_kb;

    /* Log allocation with actual node used */
    flightrec_log(TRACE_EVT_MEM_ALLOC, c->job_id, result.node, 1);
  }

  return (paddr_t)result.addr;
}

void contract_free_page(task_contract_t *c, paddr_t addr) {
  if (addr == 0)
    return;

  /* Get node before freeing (for logging) */
  uint8_t node = zenedge_get_node((zphys_t)addr);

  /* Free using portable abstraction */
  zenedge_free_page((zphys_t)addr);

  /* Credit memory back */
  if (c->mem_used_kb >= PAGE_SIZE / 1024) {
    c->mem_used_kb -= PAGE_SIZE / 1024;
  }

  flightrec_log(TRACE_EVT_MEM_FREE, c->job_id, node, 1);
}

int contract_can_continue(const task_contract_t *c) {
  /* In safe mode, no further execution allowed */
  if (c->state == CONTRACT_STATE_SAFE_MODE) {
    return 0;
  }
  return 1;
}

void contract_set_state(task_contract_t *c, contract_state_t new_state) {
  if (c->state == new_state)
    return;

  contract_state_t old_state = c->state;
  c->state = new_state;

  console_write("[contracts] job ");
  print_uint(c->job_id);
  console_write(" state: ");
  console_write(contract_state_name(old_state));
  console_write(" -> ");
  console_write(contract_state_name(new_state));
  console_write("\n");

  /* Log state transition */
  flightrec_log(TRACE_EVT_CONTRACT_STATE_CHANGE, c->job_id, (uint32_t)old_state,
                (uint32_t)new_state);

  if (new_state == CONTRACT_STATE_SAFE_MODE) {
    flightrec_log(TRACE_EVT_CONTRACT_SAFE_MODE, c->job_id, 0,
                  c->cpu_violations + c->mem_violations);
  }
}

void contract_debug_print(const task_contract_t *c) {
  console_write("[contracts] Job ");
  print_uint(c->job_id);
  console_write(": state=");
  console_write(contract_state_name(c->state));
  console_write(", cpu=");
  print_uint(c->cpu_used_us);
  console_write("/");
  print_uint(c->cpu_budget_us);
  console_write("us, mem=");
  print_uint(c->mem_used_kb);
  console_write("/");
  print_uint(c->memory_kb);
  console_write("KB\n");
}

/* ========================================================================
 * Admission Control
 * ======================================================================== */

#include "job/job_graph.h"

const char *admit_result_name(admit_result_t result) {
  switch (result) {
  case ADMIT_OK:
    return "OK";
  case ADMIT_REJECT_MEMORY:
    return "REJECT_MEMORY";
  case ADMIT_REJECT_CPU:
    return "REJECT_CPU";
  case ADMIT_REJECT_PRIORITY:
    return "REJECT_PRIORITY";
  case ADMIT_REJECT_NO_RESOURCES:
    return "REJECT_NO_RESOURCES";
  default:
    return "UNKNOWN";
  }
}

admit_result_t contract_admit_job(const task_contract_t *c,
                                  const struct job_graph *job) {
  /* Must have computed memory metrics first */
  const job_graph_t *jg = (const job_graph_t *)job;

  console_write("[admit] checking job ");
  print_uint(jg->id);
  console_write(" against contract for job ");
  print_uint(c->job_id);
  console_write("\n");

  /* Check 1: Peak memory must fit in contract budget */
  console_write("[admit] job peak memory: ");
  print_uint(jg->peak_memory_kb);
  console_write("KB, contract budget: ");
  print_uint(c->memory_kb);
  console_write("KB\n");

  if (jg->peak_memory_kb > c->memory_kb) {
    console_write("[admit] REJECTED: memory budget exceeded\n");
    flightrec_log(TRACE_EVT_JOB_REJECT, jg->id, ADMIT_REJECT_MEMORY,
                  jg->peak_memory_kb);
    return ADMIT_REJECT_MEMORY;
  }

  /* Check 2: Total pinned memory check (can't evict these) */
  if (jg->pinned_memory_kb > c->memory_kb) {
    console_write("[admit] REJECTED: pinned memory exceeds budget\n");
    flightrec_log(TRACE_EVT_JOB_REJECT, jg->id, ADMIT_REJECT_MEMORY,
                  jg->pinned_memory_kb);
    return ADMIT_REJECT_MEMORY;
  }

  /* Check 3: Already at memory limits? */
  uint32_t available_kb = c->memory_kb - c->mem_used_kb;
  if (jg->peak_memory_kb > available_kb) {
    console_write("[admit] REJECTED: insufficient available memory (");
    print_uint(available_kb);
    console_write("KB free)\n");
    flightrec_log(TRACE_EVT_JOB_REJECT, jg->id, ADMIT_REJECT_NO_RESOURCES,
                  available_kb);
    return ADMIT_REJECT_NO_RESOURCES;
  }

  /* Check 4: Estimate CPU time based on step count
   * Simple heuristic: ~1000us per compute step, ~3000us per collective
   */
  uint32_t estimated_cpu_us = 0;
  for (uint8_t i = 0; i < jg->num_steps; i++) {
    const job_step_t *s = &jg->steps[i];
    switch (s->type) {
    case STEP_TYPE_COMPUTE:
      estimated_cpu_us += 1000;
      break;
    case STEP_TYPE_COLLECTIVE:
      estimated_cpu_us += 3000;
      break;
    case STEP_TYPE_IO:
      estimated_cpu_us += 2000;
      break;
    case STEP_TYPE_CONTROL:
      estimated_cpu_us += 100;
      break;
    }
  }

  console_write("[admit] estimated CPU: ");
  print_uint(estimated_cpu_us);
  console_write("us, budget: ");
  print_uint(c->cpu_budget_us);
  console_write("us\n");

  if (estimated_cpu_us > c->cpu_budget_us) {
    console_write("[admit] WARNING: job may exceed CPU budget\n");
    /* Warning only - don't reject, just log */
    flightrec_log(TRACE_EVT_CONTRACT_BUDGET_WARN, jg->id, 0, estimated_cpu_us);
  }

  console_write("[admit] job ");
  print_uint(jg->id);
  console_write(" ADMITTED\n");

  flightrec_log(TRACE_EVT_JOB_ADMIT, jg->id, jg->peak_memory_kb,
                estimated_cpu_us);
  return ADMIT_OK;
}
