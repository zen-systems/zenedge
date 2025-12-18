/* kernel/job/job_graph.h
 *
 * Job Graph: DAG-based representation of AI/ML workloads
 *
 * Key features:
 * - Steps represent compute/collective/IO operations
 * - Dependencies form a DAG that mirrors the computation graph
 * - Tensor metadata tracks memory requirements per step
 * - Enables admission control and memory planning
 */
#ifndef JOB_GRAPH_H
#define JOB_GRAPH_H

#include <stdint.h>
#include <stddef.h>

/* A "job" = training run or inference service instance */
typedef uint32_t job_id_t;
typedef uint32_t step_id_t;
typedef uint32_t tensor_id_t;

/* Step types: training/inference phases, collectives, IO-heavy, etc. */
typedef enum {
    STEP_TYPE_COMPUTE = 0,      /* Matrix ops, activations */
    STEP_TYPE_COLLECTIVE,       /* AllReduce, AllGather, etc. */
    STEP_TYPE_IO,               /* Data loading, checkpointing */
    STEP_TYPE_CONTROL           /* Synchronization, barriers */
} step_type_t;

/* Tensor data types for memory estimation */
typedef enum {
    TENSOR_DTYPE_FP32 = 0,      /* 4 bytes per element */
    TENSOR_DTYPE_FP16,          /* 2 bytes per element */
    TENSOR_DTYPE_BF16,          /* 2 bytes per element */
    TENSOR_DTYPE_INT8,          /* 1 byte per element */
    TENSOR_DTYPE_INT32          /* 4 bytes per element */
} tensor_dtype_t;

/* Tensor descriptor - represents a memory buffer */
typedef struct {
    tensor_id_t     id;
    tensor_dtype_t  dtype;
    uint32_t        num_elements;   /* Total elements in tensor */
    uint32_t        size_bytes;     /* Computed size */
    uint8_t         pinned;         /* Must remain in memory */
    uint8_t         node_affinity;  /* Preferred NUMA node (0xFF = any) */
} tensor_desc_t;

/* Maximum tensors per step */
#define MAX_STEP_INPUTS  4
#define MAX_STEP_OUTPUTS 2

/* Very simple dependency model: each step waits on up to N parents */
#define MAX_STEP_DEPS 4

typedef struct {
    step_id_t   id;
    step_type_t type;

    /* Dependency tracking */
    uint8_t     num_deps;
    step_id_t   deps[MAX_STEP_DEPS];

    /* Tensor inputs/outputs for memory tracking */
    uint8_t     num_inputs;
    tensor_id_t inputs[MAX_STEP_INPUTS];    /* Tensors this step reads */
    uint8_t     num_outputs;
    tensor_id_t outputs[MAX_STEP_OUTPUTS];  /* Tensors this step produces */

    /* Memory estimation (computed from tensor metadata) */
    uint32_t    peak_memory_kb;     /* Peak memory during step execution */
    uint32_t    working_set_kb;     /* Active memory footprint */

    /* State flags */
    uint8_t     ready;        /* all deps satisfied */
    uint8_t     completed;
} job_step_t;

#define MAX_JOB_STEPS   32
#define MAX_JOB_TENSORS 64

typedef struct job_graph {
    job_id_t    id;
    uint8_t     num_steps;
    job_step_t  steps[MAX_JOB_STEPS];

    /* Tensor registry - all tensors used by this job */
    uint8_t         num_tensors;
    tensor_desc_t   tensors[MAX_JOB_TENSORS];

    /* Memory metrics (computed from tensor analysis) */
    uint32_t    total_memory_kb;        /* Sum of all tensor sizes */
    uint32_t    peak_memory_kb;         /* Max concurrent memory */
    uint32_t    pinned_memory_kb;       /* Memory that can't be evicted */

    /* Later: contract pointer, accel requirements, fabric topology hints */
} job_graph_t;

/* ========================================================================
 * Basic job graph operations
 * ======================================================================== */

void job_graph_init(job_graph_t *job, job_id_t id);
int  job_graph_add_step(job_graph_t *job,
                        step_id_t id,
                        step_type_t type);
int  job_graph_add_dep(job_graph_t *job,
                       step_id_t step,
                       step_id_t depends_on);

/* Called when a step completes: updates ready flags for dependents */
void job_graph_mark_completed(job_graph_t *job, step_id_t step);

/* Return next ready (not completed) step, or -1 if none */
int  job_graph_next_ready(const job_graph_t *job);

/* ========================================================================
 * Tensor metadata operations
 * ======================================================================== */

/* Register a tensor with the job
 * Returns: 0 on success, -1 on failure (table full or duplicate)
 */
int job_graph_add_tensor(job_graph_t *job,
                         tensor_id_t id,
                         tensor_dtype_t dtype,
                         uint32_t num_elements,
                         uint8_t pinned,
                         uint8_t node_affinity);

/* Assign tensor as input to a step
 * Returns: 0 on success, -1 on failure
 */
int job_graph_step_add_input(job_graph_t *job,
                             step_id_t step_id,
                             tensor_id_t tensor_id);

/* Assign tensor as output from a step
 * Returns: 0 on success, -1 on failure
 */
int job_graph_step_add_output(job_graph_t *job,
                              step_id_t step_id,
                              tensor_id_t tensor_id);

/* Compute memory metrics for all steps
 * Must be called after all tensors and step I/O are configured
 */
void job_graph_compute_memory(job_graph_t *job);

/* Get tensor by ID (returns NULL if not found) */
tensor_desc_t* job_graph_get_tensor(job_graph_t *job, tensor_id_t id);

/* Get step by ID (returns NULL if not found) */
job_step_t* job_graph_get_step(job_graph_t *job, step_id_t id);

/* Helper: compute tensor size in bytes */
static inline uint32_t tensor_size_bytes(tensor_dtype_t dtype, uint32_t elements) {
    uint32_t elem_size = 4; /* default FP32 */
    switch (dtype) {
        case TENSOR_DTYPE_FP32:  elem_size = 4; break;
        case TENSOR_DTYPE_FP16:  elem_size = 2; break;
        case TENSOR_DTYPE_BF16:  elem_size = 2; break;
        case TENSOR_DTYPE_INT8:  elem_size = 1; break;
        case TENSOR_DTYPE_INT32: elem_size = 4; break;
    }
    return elements * elem_size;
}

#endif

