/* kernel/job/job_graph.c
 *
 * Job Graph implementation with tensor metadata tracking
 */
#include "job_graph.h"

void job_graph_init(job_graph_t *job, job_id_t id) {
    job->id = id;
    job->num_steps = 0;
    job->num_tensors = 0;
    job->total_memory_kb = 0;
    job->peak_memory_kb = 0;
    job->pinned_memory_kb = 0;
}

static job_step_t *find_step(job_graph_t *job, step_id_t id) {
    for (uint8_t i = 0; i < job->num_steps; i++) {
        if (job->steps[i].id == id) {
            return &job->steps[i];
        }
    }
    return NULL;
}

int job_graph_add_step(job_graph_t *job,
                       step_id_t id,
                       step_type_t type)
{
    if (job->num_steps >= MAX_JOB_STEPS)
        return -1;

    job_step_t *s = &job->steps[job->num_steps++];
    s->id        = id;
    s->type      = type;
    s->num_deps  = 0;
    s->ready     = 1;  /* no deps yet */
    s->completed = 0;

    /* Initialize tensor tracking */
    s->num_inputs = 0;
    s->num_outputs = 0;
    s->peak_memory_kb = 0;
    s->working_set_kb = 0;

    return 0;
}

int job_graph_add_dep(job_graph_t *job,
                      step_id_t step,
                      step_id_t depends_on)
{
    job_step_t *s  = find_step(job, step);
    job_step_t *dp = find_step(job, depends_on);
    if (!s || !dp)
        return -1;

    if (s->num_deps >= MAX_STEP_DEPS)
        return -1;

    s->deps[s->num_deps++] = depends_on;
    /* step not ready until dep is completed */
    s->ready = 0;
    return 0;
}

void job_graph_mark_completed(job_graph_t *job, step_id_t step) {
    job_step_t *s = find_step(job, step);
    if (!s) return;
    s->completed = 1;

    /* naive: rescan all steps; fine for small job_graph */
    for (uint8_t i = 0; i < job->num_steps; i++) {
        job_step_t *t = &job->steps[i];
        if (t->completed) continue;

        uint8_t all_done = 1;
        for (uint8_t d = 0; d < t->num_deps; d++) {
            job_step_t *dep = find_step(job, t->deps[d]);
            if (!dep || !dep->completed) {
                all_done = 0;
                break;
            }
        }
        if (all_done)
            t->ready = 1;
    }
}

int job_graph_next_ready(const job_graph_t *job) {
    for (uint8_t i = 0; i < job->num_steps; i++) {
        const job_step_t *s = &job->steps[i];
        if (s->ready && !s->completed)
            return (int)s->id;
    }
    return -1;
}

/* ========================================================================
 * Tensor metadata operations
 * ======================================================================== */

static tensor_desc_t *find_tensor(job_graph_t *job, tensor_id_t id) {
    for (uint8_t i = 0; i < job->num_tensors; i++) {
        if (job->tensors[i].id == id) {
            return &job->tensors[i];
        }
    }
    return NULL;
}

int job_graph_add_tensor(job_graph_t *job,
                         tensor_id_t id,
                         tensor_dtype_t dtype,
                         uint32_t num_elements,
                         uint8_t pinned,
                         uint8_t node_affinity)
{
    if (job->num_tensors >= MAX_JOB_TENSORS)
        return -1;

    /* Check for duplicate */
    if (find_tensor(job, id))
        return -1;

    tensor_desc_t *t = &job->tensors[job->num_tensors++];
    t->id = id;
    t->dtype = dtype;
    t->num_elements = num_elements;
    t->size_bytes = tensor_size_bytes(dtype, num_elements);
    t->pinned = pinned;
    t->node_affinity = node_affinity;

    return 0;
}

int job_graph_step_add_input(job_graph_t *job,
                             step_id_t step_id,
                             tensor_id_t tensor_id)
{
    job_step_t *s = find_step(job, step_id);
    if (!s)
        return -1;

    /* Verify tensor exists */
    if (!find_tensor(job, tensor_id))
        return -1;

    if (s->num_inputs >= MAX_STEP_INPUTS)
        return -1;

    s->inputs[s->num_inputs++] = tensor_id;
    return 0;
}

int job_graph_step_add_output(job_graph_t *job,
                              step_id_t step_id,
                              tensor_id_t tensor_id)
{
    job_step_t *s = find_step(job, step_id);
    if (!s)
        return -1;

    /* Verify tensor exists */
    if (!find_tensor(job, tensor_id))
        return -1;

    if (s->num_outputs >= MAX_STEP_OUTPUTS)
        return -1;

    s->outputs[s->num_outputs++] = tensor_id;
    return 0;
}

void job_graph_compute_memory(job_graph_t *job) {
    uint32_t total = 0;
    uint32_t pinned = 0;
    uint32_t peak = 0;

    /* Calculate total and pinned memory from tensors */
    for (uint8_t i = 0; i < job->num_tensors; i++) {
        tensor_desc_t *t = &job->tensors[i];
        uint32_t size_kb = (t->size_bytes + 1023) / 1024;
        total += size_kb;
        if (t->pinned)
            pinned += size_kb;
    }

    /* Calculate per-step memory (inputs + outputs active during execution) */
    for (uint8_t i = 0; i < job->num_steps; i++) {
        job_step_t *s = &job->steps[i];
        uint32_t step_mem = 0;

        /* Sum input tensor sizes */
        for (uint8_t j = 0; j < s->num_inputs; j++) {
            tensor_desc_t *t = find_tensor(job, s->inputs[j]);
            if (t) {
                step_mem += (t->size_bytes + 1023) / 1024;
            }
        }

        /* Sum output tensor sizes */
        for (uint8_t j = 0; j < s->num_outputs; j++) {
            tensor_desc_t *t = find_tensor(job, s->outputs[j]);
            if (t) {
                step_mem += (t->size_bytes + 1023) / 1024;
            }
        }

        s->working_set_kb = step_mem;
        s->peak_memory_kb = step_mem; /* Could be higher with intermediates */

        if (step_mem > peak)
            peak = step_mem;
    }

    job->total_memory_kb = total;
    job->pinned_memory_kb = pinned;
    job->peak_memory_kb = peak;
}

tensor_desc_t* job_graph_get_tensor(job_graph_t *job, tensor_id_t id) {
    return find_tensor(job, id);
}

job_step_t* job_graph_get_step(job_graph_t *job, step_id_t id) {
    return find_step(job, id);
}

