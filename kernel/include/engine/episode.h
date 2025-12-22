#ifndef _ENGINE_EPISODE_H
#define _ENGINE_EPISODE_H

#include "../api/collector.h"
#include <stdint.h>

/* Tuning Episode States */
typedef enum {
  EP_STATE_IDLE,     /* No active episode */
  EP_STATE_PROPOSE,  /* Requesting new params */
  EP_STATE_VALIDATE, /* Checking static constraints */
  EP_STATE_APPLY,    /* Applying to Actuator */
  EP_STATE_MONITOR,  /* Gathering telemetry */
  EP_STATE_DECIDE,   /* Comparing metrics vs baseline */
  EP_STATE_ROLLBACK, /* Reverting due to violation/regression */
  EP_STATE_SAFE      /* System in known safe state */
} episode_state_t;

/* Outcome Codes */
typedef enum {
  OUTCOME_NONE,
  OUTCOME_PROMOTED,
  OUTCOME_REJECTED_REGRESSION,
  OUTCOME_REJECTED_VIOLATION,
  OUTCOME_FAILED_ACTUATOR
} episode_outcome_t;

/* Context for a single tuning episode */
typedef struct {
  uint32_t episode_id;
  episode_state_t state;

  /* Configuration */
  uint32_t monitor_steps_total;
  uint32_t monitor_steps_done;

  /* Metrics Accumulator */
  metric_snapshot_t baseline;
  metric_snapshot_t current_accum; /* Sum of metrics */
  uint32_t samples;

  /* Latency Tracking */
  uint64_t start_time;
  uint64_t end_time;

  /* Proposed Changes */
  uint32_t proposed_clock;
  uint32_t original_clock; /* For rollback */
} episode_ctx_t;

/* Public API */
void episode_init(void);
void episode_tick(void); /* Called by scheduler/timer */
int episode_propose(uint32_t clock_mhz, uint32_t duration_steps);
episode_ctx_t *episode_get_current(void);

#endif /* _ENGINE_EPISODE_H */
