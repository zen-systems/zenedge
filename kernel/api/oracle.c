/* kernel/api/oracle.c */
#include "../contracts.h"
#include "../include/oracle.h"

verdict_t get_job_verdict(uint64_t job_id) {
    const task_contract_t *c = contract_lookup((uint32_t)job_id);
    if (!c)
        return VERDICT_PASS;

    switch (c->state) {
    case CONTRACT_STATE_OK:
        return VERDICT_PASS;
    case CONTRACT_STATE_WARNED:
        return VERDICT_THROTTLE;
    case CONTRACT_STATE_SAFE_MODE:
        return VERDICT_KILL;
    default:
        return VERDICT_KILL;
    }
}
