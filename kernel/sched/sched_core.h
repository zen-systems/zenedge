/* kernel/sched/sched_core.h */
#ifndef SCHED_CORE_H
#define SCHED_CORE_H

#include "../contracts.h"
#include "../job/job_graph.h"
#include "../process.h"
#include <stdint.h>

typedef struct {
  job_graph_t *job;
  task_contract_t contract;

  /* later: per-step runtime stats, device selections, etc. */
} sched_job_ctx_t;

void sched_init(void);
void sched_run_job(sched_job_ctx_t *ctx);
void sched_test_rr(void);
void schedule(void);

/* Process Model */
process_t *sched_create_user_process(uint32_t entry_point, uint32_t wasm_blob_phys, uint32_t wasm_size);
void sched_destroy_process(process_t *proc);

#endif /* SCHED_CORE_H */
