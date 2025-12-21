/* kernel/api/contract_registry.c */
#include "../contracts.h"

#define CONTRACT_REGISTRY_MAX 64
static task_contract_t *contract_registry[CONTRACT_REGISTRY_MAX];
static uint32_t contract_registry_count = 0;

void contract_register(task_contract_t *c) {
    if (!c)
        return;

    for (uint32_t i = 0; i < contract_registry_count; i++) {
        if (contract_registry[i] && contract_registry[i]->job_id == c->job_id) {
            contract_registry[i] = c;
            return;
        }
    }

    if (contract_registry_count < CONTRACT_REGISTRY_MAX) {
        contract_registry[contract_registry_count++] = c;
    }
}

const task_contract_t *contract_lookup(uint32_t job_id) {
    for (uint32_t i = 0; i < contract_registry_count; i++) {
        const task_contract_t *c = contract_registry[i];
        if (c && c->job_id == job_id)
            return c;
    }
    return NULL;
}
