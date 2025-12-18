/* kernel/trace/flightrec.c
 *
 * Flight Recorder implementation with real timestamps and duration tracking.
 */
#include "flightrec.h"
#include "../console.h"
#include "../time/time.h"

/* Ring buffer */
static trace_event_t buf[TRACE_BUF_SIZE];
static uint32_t head = 0;
static uint8_t  initialized = 0;

/* Span tracking for duration measurement (simple array for now) */
#define MAX_ACTIVE_SPANS 16
typedef struct {
    uint8_t  active;
    uint32_t job_id;
    uint32_t step_id;
    uint64_t start_cycles;
    uint8_t  start_type;
} active_span_t;

static active_span_t spans[MAX_ACTIVE_SPANS];
static uint32_t next_span_handle = 1;

/* Integer to string helper */
static void print_uint64(uint64_t v) {
    char tmp[21];
    int i = 19;
    tmp[20] = '\0';
    if (v == 0) {
        console_write("0");
        return;
    }
    while (v > 0 && i >= 0) {
        tmp[i--] = '0' + (v % 10);
        v /= 10;
    }
    console_write(&tmp[i + 1]);
}

static void print_uint32(uint32_t v) {
    print_uint64((uint64_t)v);
}

static void print_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    char buf[3] = { hex[(v >> 4) & 0xF], hex[v & 0xF], '\0' };
    console_write(buf);
}

void flightrec_init(void) {
    head = 0;
    initialized = 1;

    /* Clear span tracking */
    for (int i = 0; i < MAX_ACTIVE_SPANS; i++) {
        spans[i].active = 0;
    }
    next_span_handle = 1;

    console_write("[trace] flight recorder initialized (");
    print_uint32(TRACE_BUF_SIZE);
    console_write(" event ring buffer)\n");

    /* Log boot event */
    flightrec_log(TRACE_EVT_BOOT, 0, 0, 0);
}

void flightrec_log(trace_event_type_t type, uint32_t job_id,
                   uint32_t step_id, uint32_t extra) {
    if (!initialized) return;

    trace_event_t *e = &buf[head & TRACE_BUF_MASK];

    e->ts_cycles = time_cycles();
    e->ts_usec   = time_usec();
    e->type      = (uint8_t)type;
    e->flags     = 0;
    e->cpu_id    = 0;  /* Single CPU for now */
    e->job_id    = job_id;
    e->step_id   = step_id;
    e->extra     = extra;

    head++;
}

trace_span_t flightrec_begin_span(trace_event_type_t start_type,
                                   uint32_t job_id, uint32_t step_id) {
    if (!initialized) return 0;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_SPANS; i++) {
        if (!spans[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        /* No free slots - log warning and return invalid handle */
        flightrec_log(TRACE_EVT_CONTRACT_VIOLATION, job_id, step_id, 0xDEAD);
        return 0;
    }

    spans[slot].active = 1;
    spans[slot].job_id = job_id;
    spans[slot].step_id = step_id;
    spans[slot].start_cycles = time_cycles();
    spans[slot].start_type = (uint8_t)start_type;

    /* Log start event */
    flightrec_log(start_type, job_id, step_id, 0);

    /* Return handle (slot + 1 so 0 is invalid) */
    return (trace_span_t)(slot + 1);
}

void flightrec_end_span(trace_span_t span, trace_event_type_t end_type) {
    if (!initialized || span == 0) return;

    int slot = (int)span - 1;
    if (slot < 0 || slot >= MAX_ACTIVE_SPANS || !spans[slot].active) {
        return;
    }

    /* Calculate duration */
    uint64_t end_cycles = time_cycles();
    uint64_t elapsed_cycles = end_cycles - spans[slot].start_cycles;
    usec_t duration_usec = cycles_to_usec(elapsed_cycles);

    /* Log end event with duration */
    flightrec_log(end_type, spans[slot].job_id, spans[slot].step_id,
                  (uint32_t)duration_usec);

    /* Free slot */
    spans[slot].active = 0;
}

usec_t flightrec_last_duration(uint32_t job_id, uint32_t step_id) {
    /* Scan buffer backwards for matching STEP_END event */
    uint32_t count = (head > TRACE_BUF_SIZE) ? TRACE_BUF_SIZE : head;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (head - 1 - i) & TRACE_BUF_MASK;
        trace_event_t *e = &buf[idx];

        if (e->type == TRACE_EVT_STEP_END &&
            e->job_id == job_id &&
            e->step_id == step_id) {
            return (usec_t)e->extra;
        }
    }
    return 0;
}

void flightrec_get_job_stats(uint32_t job_id, trace_job_stats_t *out) {
    if (!out) return;

    out->job_id = job_id;
    out->steps_completed = 0;
    out->total_cpu_usec = 0;
    out->total_wall_usec = 0;
    out->violations = 0;

    uint32_t count = (head > TRACE_BUF_SIZE) ? TRACE_BUF_SIZE : head;
    uint64_t first_ts = 0, last_ts = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (head > TRACE_BUF_SIZE) ?
                       ((head - TRACE_BUF_SIZE + i) & TRACE_BUF_MASK) :
                       i;
        trace_event_t *e = &buf[idx];

        if (e->job_id != job_id) continue;

        /* Track wall time span */
        if (first_ts == 0 || e->ts_usec < first_ts) first_ts = e->ts_usec;
        if (e->ts_usec > last_ts) last_ts = e->ts_usec;

        switch (e->type) {
            case TRACE_EVT_STEP_END:
                out->steps_completed++;
                out->total_cpu_usec += e->extra;  /* Duration stored in extra */
                break;

            case TRACE_EVT_CONTRACT_VIOLATION:
            case TRACE_EVT_CONTRACT_BUDGET_EXCEED:
                out->violations++;
                break;

            default:
                break;
        }
    }

    out->total_wall_usec = last_ts - first_ts;
}

static const char* event_type_name(uint8_t type) {
    switch (type) {
        case TRACE_EVT_SCHED_TICK:           return "SCHED_TICK";
        case TRACE_EVT_JOB_SUBMIT:           return "JOB_SUBMIT";
        case TRACE_EVT_JOB_COMPLETE:         return "JOB_COMPLETE";
        case TRACE_EVT_STEP_START:           return "STEP_START";
        case TRACE_EVT_STEP_END:             return "STEP_END";
        case TRACE_EVT_STEP_PREEMPT:         return "STEP_PREEMPT";
        case TRACE_EVT_CONTRACT_APPLY:       return "CONTRACT_APPLY";
        case TRACE_EVT_CONTRACT_BUDGET_WARN: return "BUDGET_WARN";
        case TRACE_EVT_CONTRACT_BUDGET_EXCEED: return "BUDGET_EXCEED";
        case TRACE_EVT_CONTRACT_VIOLATION:   return "VIOLATION";
        case TRACE_EVT_CONTRACT_STATE_CHANGE: return "STATE_CHANGE";
        case TRACE_EVT_CONTRACT_SAFE_MODE:   return "SAFE_MODE";
        case TRACE_EVT_BOOT:                 return "BOOT";
        case TRACE_EVT_HALT:                 return "HALT";
        case TRACE_EVT_PANIC:                return "PANIC";
        /* Memory events */
        case TRACE_EVT_MEM_ALLOC:            return "MEM_ALLOC";
        case TRACE_EVT_MEM_FREE:             return "MEM_FREE";
        case TRACE_EVT_MEM_ALLOC_FAIL:       return "MEM_ALLOC_FAIL";
        case TRACE_EVT_MEM_LOCALITY_MISS:    return "LOCALITY_MISS";
        case TRACE_EVT_MEM_CONTRACT_EXCEED:  return "MEM_EXCEED";
        case TRACE_EVT_MEM_NODE_UNSUPPORTED: return "NODE_UNSUP";
        default:                             return "UNKNOWN";
    }
}

void flightrec_dump_console(void) {
    console_write("\n=== FLIGHT RECORDER DUMP ===\n");
    console_write("TIME(us)     | TYPE             | JOB  | STEP | EXTRA\n");
    console_write("-------------|------------------|------|------|--------\n");

    uint32_t count = (head > TRACE_BUF_SIZE) ? TRACE_BUF_SIZE : head;
    uint32_t start = (head > TRACE_BUF_SIZE) ? (head - TRACE_BUF_SIZE) : 0;

    for (uint32_t i = 0; i < count; i++) {
        trace_event_t *e = &buf[(start + i) & TRACE_BUF_MASK];

        /* Timestamp */
        print_uint64(e->ts_usec);
        console_write(" | ");

        /* Event type */
        console_write(event_type_name(e->type));
        console_write(" | ");

        /* Job ID */
        print_uint32(e->job_id);
        console_write(" | ");

        /* Step ID */
        print_uint32(e->step_id);
        console_write(" | ");

        /* Extra (duration for END events) */
        if (e->type == TRACE_EVT_STEP_END) {
            print_uint32(e->extra);
            console_write("us");
        } else if (e->extra != 0) {
            console_write("0x");
            print_hex8((e->extra >> 24) & 0xFF);
            print_hex8((e->extra >> 16) & 0xFF);
            print_hex8((e->extra >> 8) & 0xFF);
            print_hex8(e->extra & 0xFF);
        } else {
            console_write("-");
        }

        console_write("\n");
    }

    console_write("=== END DUMP (");
    print_uint32(count);
    console_write(" events) ===\n\n");
}

void flightrec_dump_filtered(uint8_t type_mask_lo, uint8_t type_mask_hi) {
    uint32_t count = (head > TRACE_BUF_SIZE) ? TRACE_BUF_SIZE : head;
    uint32_t start = (head > TRACE_BUF_SIZE) ? (head - TRACE_BUF_SIZE) : 0;
    uint32_t matched = 0;

    console_write("\n=== FILTERED TRACE (types 0x");
    print_hex8(type_mask_lo);
    console_write("-0x");
    print_hex8(type_mask_hi);
    console_write(") ===\n");

    for (uint32_t i = 0; i < count; i++) {
        trace_event_t *e = &buf[(start + i) & TRACE_BUF_MASK];

        if (e->type >= type_mask_lo && e->type <= type_mask_hi) {
            print_uint64(e->ts_usec);
            console_write(" ");
            console_write(event_type_name(e->type));
            console_write(" j=");
            print_uint32(e->job_id);
            console_write(" s=");
            print_uint32(e->step_id);
            console_write("\n");
            matched++;
        }
    }

    console_write("=== ");
    print_uint32(matched);
    console_write(" events matched ===\n\n");
}

const trace_event_t* flightrec_get_buffer(uint32_t *out_head, uint32_t *out_count) {
    if (out_head) *out_head = head;
    if (out_count) *out_count = (head > TRACE_BUF_SIZE) ? TRACE_BUF_SIZE : head;
    return buf;
}
