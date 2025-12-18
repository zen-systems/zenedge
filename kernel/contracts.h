/* kernel/contracts.h
 *
 * Contract-based resource governance for ZENEDGE
 *
 * Contracts define resource budgets (CPU, memory, accelerators) and
 * the system enforces them through tracking and state machine transitions.
 */
#ifndef CONTRACTS_H
#define CONTRACTS_H

#include <stdint.h>
#include "mm/pmm.h"

/* Priority levels for scheduling decisions */
typedef enum {
    CONTRACT_PRIORITY_LOW = 0,
    CONTRACT_PRIORITY_NORMAL,
    CONTRACT_PRIORITY_HIGH,
    CONTRACT_PRIORITY_REALTIME
} contract_priority_t;

/* Contract enforcement state machine */
typedef enum {
    CONTRACT_STATE_OK = 0,      /* Within all budgets */
    CONTRACT_STATE_WARNED,      /* First violation detected */
    CONTRACT_STATE_SAFE_MODE    /* Multiple violations, restricted execution */
} contract_state_t;

/* Memory tier hints (for future HBM/device memory) */
typedef enum {
    MEM_TIER_DDR = 0,           /* Regular system RAM */
    MEM_TIER_HBM,               /* High-bandwidth memory */
    MEM_TIER_DEVICE             /* Accelerator local memory */
} mem_tier_t;

/* Task contract - resource budget and runtime accounting */
typedef struct {
    /* Resource budgets */
    uint32_t cpu_budget_us;     /* CPU time per scheduling window */
    uint32_t memory_kb;         /* Memory soft cap */
    uint32_t accel_slots;       /* Abstract "accelerator tokens" */
    contract_priority_t prio;   /* Scheduling priority */

    /* NUMA and memory hints */
    uint8_t preferred_node;     /* Preferred NUMA node */
    mem_tier_t tier_hint;       /* Memory tier preference */

    /* Runtime accounting (updated by system) */
    uint32_t cpu_used_us;       /* CPU time consumed */
    uint32_t mem_used_kb;       /* Memory allocated */
    uint32_t cpu_violations;    /* CPU budget exceed count */
    uint32_t mem_violations;    /* Memory budget exceed count */

    /* State machine */
    contract_state_t state;     /* Current enforcement state */
    uint32_t job_id;            /* Associated job for tracing */
} task_contract_t;

/* Initialize contract system */
void contracts_init(void);

/* Apply a contract (sets up tracking, logs event) */
void contract_apply(task_contract_t *c);

/* Charge CPU time to a contract
 * Returns: 0 if within budget, 1 if exceeded (violation logged)
 */
int contract_charge_cpu(task_contract_t *c, uint32_t usec);

/* Charge memory to a contract
 * Returns: 0 if within budget, 1 if exceeded (violation logged)
 */
int contract_charge_memory(task_contract_t *c, uint32_t kb);

/* Allocate a page through a contract (charges memory, respects node pref)
 * Returns: physical address or 0 on failure
 */
paddr_t contract_alloc_page(task_contract_t *c);

/* Free a page and credit the contract */
void contract_free_page(task_contract_t *c, paddr_t addr);

/* Check if contract allows further execution */
int contract_can_continue(const task_contract_t *c);

/* Transition contract state (logs event) */
void contract_set_state(task_contract_t *c, contract_state_t new_state);

/* Get a string name for the state */
const char* contract_state_name(contract_state_t state);

/* Debug: print contract details */
void contract_debug_print(const task_contract_t *c);

/* ========================================================================
 * Admission Control
 * ======================================================================== */

/* Admission result codes */
typedef enum {
    ADMIT_OK = 0,               /* Job can be admitted */
    ADMIT_REJECT_MEMORY,        /* Exceeds memory budget */
    ADMIT_REJECT_CPU,           /* Exceeds CPU budget (estimated) */
    ADMIT_REJECT_PRIORITY,      /* Priority conflict */
    ADMIT_REJECT_NO_RESOURCES   /* System resources exhausted */
} admit_result_t;

/* Forward declaration */
struct job_graph;

/* Check if a job can be admitted under a contract
 * Analyzes the job DAG memory requirements against contract budget
 * @param c: Contract to check against
 * @param job: Job graph with computed memory metrics
 * @return: Admission result code
 */
admit_result_t contract_admit_job(const task_contract_t *c,
                                   const struct job_graph *job);

/* Get string description of admission result */
const char* admit_result_name(admit_result_t result);

#endif /* CONTRACTS_H */
