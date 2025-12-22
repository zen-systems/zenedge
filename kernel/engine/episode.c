#include "../include/engine/episode.h"
#include "../console.h"
#include "../include/api/actuator.h"
#include "../include/api/collector.h"

static episode_ctx_t current_episode;
static int episode_active = 0;

void episode_init(void) {
  current_episode.episode_id = 0;
  current_episode.state = EP_STATE_IDLE;
  current_episode.samples = 0;
  episode_active = 0;
  console_write("[episode] Engine Initialized\n");
}

/* Helper: Check Violations */
static int check_guardrails(metric_snapshot_t *m) {
  /* Hard Limit: Temp > 90C */
  if (m->gpu_temp_c > 90) {
    console_write("[episode] VIOLATION: Temp > 90C!\n");
    return 1;
  }

  /* Hard Limit: ECC Errors > 0 */
  if (m->ecc_errors > 0) {
    console_write("[episode] VIOLATION: ECC Error detected!\n");
    return 1;
  }

  return 0; // OK
}

void episode_tick(void) {
  if (!episode_active)
    return;

  collector_t *col = collector_get_default();
  actuator_t *act = actuator_get_default();

  switch (current_episode.state) {
  case EP_STATE_PROPOSE:
    /* Move to Validate */
    current_episode.state = EP_STATE_VALIDATE;
    break;

  case EP_STATE_VALIDATE:
    /* Check static constraints (mock) */
    // console_write("[episode] Validating constraints...\n");
    current_episode.state = EP_STATE_APPLY;
    break;

  case EP_STATE_APPLY:
    if (act) {
      act->set_clock_limit(act, current_episode.proposed_clock);
      console_write("[episode] Applied Clock: ");
      print_uint(current_episode.proposed_clock);
      console_write(" MHz\n");
    }
    current_episode.state = EP_STATE_MONITOR;
    break;

  case EP_STATE_MONITOR:
    if (col) {
      metric_snapshot_t snap;
      if (col->get_snapshot(col, &snap) == 0) {
        /* Accumulate */
        current_episode.current_accum.gpu_temp_c += snap.gpu_temp_c;
        current_episode.current_accum.gpu_util_pct += snap.gpu_util_pct;
        current_episode.samples++;

        /* Guardrail Check (Fail Fast) */
        if (check_guardrails(&snap)) {
          current_episode.state = EP_STATE_ROLLBACK;
          return;
        }
      }
    }

    current_episode.monitor_steps_done++;
    if (current_episode.monitor_steps_done >=
        current_episode.monitor_steps_total) {
      current_episode.state = EP_STATE_DECIDE;
    }
    break;

  case EP_STATE_DECIDE:
    /* Decision Logic */
    {
      uint32_t avg_util = 0;
      if (current_episode.samples > 0) {
        avg_util = current_episode.current_accum.gpu_util_pct /
                   current_episode.samples;
      }

      console_write("[episode] DECISION: Avg Util = ");
      print_uint(avg_util);
      console_write("%\n");

      /* Simple Policy: Verify Util > 50% */
      if (avg_util > 50) {
        console_write("[episode] PROMOTE! Improvement confirmed.\n");
        current_episode.state = EP_STATE_IDLE;
        episode_active = 0;
      } else {
        console_write("[episode] REJECT: No improvement.\n");
        current_episode.state = EP_STATE_ROLLBACK;
      }
    }
    break;

  case EP_STATE_ROLLBACK:
    console_write("[episode] ROLLING BACK to ");
    print_uint(current_episode.original_clock);
    console_write(" MHz\n");

    if (act) {
      act->set_clock_limit(act, current_episode.original_clock);
    }
    current_episode.state = EP_STATE_IDLE;
    episode_active = 0;
    break;

  default:
    break;
  }
}

int episode_propose(uint32_t clock_mhz, uint32_t duration_steps) {
  if (episode_active)
    return -1;

  current_episode.episode_id++;
  current_episode.state = EP_STATE_PROPOSE;
  current_episode.proposed_clock = clock_mhz;
  current_episode.original_clock = 1000; /* Mock baseline */
  current_episode.monitor_steps_total = duration_steps;
  current_episode.monitor_steps_done = 0;

  /* Reset accumulators */
  current_episode.current_accum.gpu_temp_c = 0;
  current_episode.current_accum.gpu_util_pct = 0;
  current_episode.samples = 0;

  episode_active = 1;
  console_write("[episode] Proposal Accepted: Clock ");
  print_uint(clock_mhz);
  console_write("\n");
  return 0;
}

episode_ctx_t *episode_get_current(void) { return &current_episode; }
