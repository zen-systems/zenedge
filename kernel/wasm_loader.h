#ifndef WASM_LOADER_H
#define WASM_LOADER_H

#include <stdint.h>
#include <stddef.h>

void wasm_init(void);
int wasm_run_bytes(const uint8_t* code, size_t size);

#endif
