/* kernel/trace/flightrec.h
 *
 * Flight Recorder: Always-on, low-overhead telemetry for AI/ML governance.
 *
 * Design principles:
 * - Lock-free ring buffer (single producer in uniprocessor for now)
 * - Real timestamps via rdtsc
 * - Rich event types for job scheduling, contracts, memory, IO
 * - Duration tracking for contract enforcement
 * - Correlation IDs for distributed tracing
 */
#ifndef FLIGHTREC_H
#define FLIGHTREC_H

#include <stdint.h>
#include "../time/time.h"

/* Ring buffer size - must be power of 2 */
#define TRACE_BUF_SIZE 256
#define TRACE_BUF_MASK (TRACE_BUF_SIZE - 1)

/*
 * Event categories for filtering and analysis
 */
typedef enum {
    /* Scheduler events */
    TRACE_EVT_SCHED_TICK       = 0x00,
    TRACE_EVT_JOB_SUBMIT       = 0x01,
    TRACE_EVT_JOB_COMPLETE     = 0x02,
    TRACE_EVT_STEP_START       = 0x03,
    TRACE_EVT_STEP_END         = 0x04,
    TRACE_EVT_STEP_PREEMPT     = 0x05,
    TRACE_EVT_JOB_ADMIT        = 0x06,  /* Job admitted after passing checks */
    TRACE_EVT_JOB_REJECT       = 0x07,  /* Job rejected by admission control */

    /* Contract events */
    TRACE_EVT_CONTRACT_APPLY   = 0x10,
    TRACE_EVT_CONTRACT_BUDGET_WARN  = 0x11,
    TRACE_EVT_CONTRACT_BUDGET_EXCEED = 0x12,
    TRACE_EVT_CONTRACT_VIOLATION    = 0x13,
    TRACE_EVT_CONTRACT_STATE_CHANGE = 0x14,  /* State machine transition */
    TRACE_EVT_CONTRACT_SAFE_MODE    = 0x15,  /* Entered safe mode */

    /* Memory events */
    TRACE_EVT_MEM_ALLOC        = 0x20,
    TRACE_EVT_MEM_FREE         = 0x21,
    TRACE_EVT_MEM_ALLOC_FAIL   = 0x22,
    TRACE_EVT_MEM_LOCALITY_MISS = 0x23,  /* Allocated from non-preferred node */
    TRACE_EVT_MEM_CONTRACT_EXCEED = 0x24, /* Memory budget exceeded */
    TRACE_EVT_MEM_NODE_UNSUPPORTED = 0x25, /* Requested unsupported NUMA node */

    /* IO events (future) */
    TRACE_EVT_IO_SUBMIT        = 0x30,
    TRACE_EVT_IO_COMPLETE      = 0x31,
    TRACE_EVT_IO_STALL         = 0x32,

    /* Accelerator events (future) */
    TRACE_EVT_ACCEL_SUBMIT     = 0x40,
    TRACE_EVT_ACCEL_COMPLETE   = 0x41,
    TRACE_EVT_ACCEL_THROTTLE   = 0x42,

    /* Thermal/Power events (future) */
    TRACE_EVT_THERMAL_WARN     = 0x50,
    TRACE_EVT_POWER_CAP        = 0x51,

    /* System events */
    TRACE_EVT_BOOT             = 0xF0,
    TRACE_EVT_HALT             = 0xF1,
    TRACE_EVT_PANIC            = 0xFF
} trace_event_type_t;

/*
 * Trace event structure - 32 bytes for cache alignment
 *
 * For duration-based events (STEP_START/END, IO_SUBMIT/COMPLETE):
 * - START event stores start_tsc
 * - END event stores duration in 'extra' field (computed at log time)
 */
typedef struct __attribute__((packed)) {
    uint64_t ts_usec;           /* Timestamp in microseconds since boot */
    uint64_t ts_cycles;         /* Raw TSC value for high-precision deltas */
    uint8_t  type;              /* trace_event_type_t */
    uint8_t  flags;             /* Reserved for filtering/compression */
    uint16_t cpu_id;            /* CPU that logged this (0 for now) */
    uint32_t job_id;            /* Job identifier */
    uint32_t step_id;           /* Step identifier (or context-dependent) */
    uint32_t extra;             /* Duration (usec) or other context data */
} trace_event_t;

_Static_assert(sizeof(trace_event_t) == 32, "trace_event_t must be 32 bytes");

/*
 * Summary statistics for quick contract checking
 */
typedef struct {
    uint32_t job_id;
    uint32_t steps_completed;
    uint64_t total_cpu_usec;    /* Total CPU time consumed */
    uint64_t total_wall_usec;   /* Total wall time elapsed */
    uint32_t violations;        /* Contract violation count */
} trace_job_stats_t;

/* Initialize flight recorder - call after time_init() */
void flightrec_init(void);

/* Log an event (fast path) */
void flightrec_log(trace_event_type_t type, uint32_t job_id,
                   uint32_t step_id, uint32_t extra);

/*
 * Paired event logging for duration tracking
 * Returns a handle to correlate start/end events
 */
typedef uint32_t trace_span_t;

trace_span_t flightrec_begin_span(trace_event_type_t start_type,
                                   uint32_t job_id, uint32_t step_id);

void flightrec_end_span(trace_span_t span, trace_event_type_t end_type);

/*
 * Get duration of last completed span for a job/step
 * Returns duration in microseconds, or 0 if not found
 */
usec_t flightrec_last_duration(uint32_t job_id, uint32_t step_id);

/* Aggregate stats for a job (scans buffer) */
void flightrec_get_job_stats(uint32_t job_id, trace_job_stats_t *out);

/* Dump events to console (for debugging) */
void flightrec_dump_console(void);

/* Dump events matching a filter */
void flightrec_dump_filtered(uint8_t type_mask_lo, uint8_t type_mask_hi);

/* Get raw buffer access for external export */
const trace_event_t* flightrec_get_buffer(uint32_t *out_head, uint32_t *out_count);

#endif /* FLIGHTREC_H */
