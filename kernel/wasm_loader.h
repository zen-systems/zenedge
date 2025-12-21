#ifndef WASM_LOADER_H
#define WASM_LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void wasm_init(void);
/* Run bytes with optional agent args */
/* If result_ptr provided, returns action */
int wasm_run_bytes(const uint8_t* code, size_t size);

/* Run an agent step: passed obs -> returns action */
int wasm_run_agent(const uint8_t* code, size_t size, const float* obs, size_t obs_len, uint32_t model_id);

/* Kernel-local inference using cached weights */
int kernel_infer_action(const float* obs, size_t obs_len, uint32_t model_id);

/* Access cached profile (weights) used by wasm_inference */
const float* wasm_get_profile(uint32_t *model_id, uint16_t *len);

#ifdef __cplusplus
}
#endif

#endif
