/* kernel/wasm/host_funcs.c */
#include "../lib/wasm3/wasm3.h"
#include "../lib/wasm3/m3_env.h"
#include "../drivers/ivshmem.h"
#include <stdint.h>

m3ApiRawFunction(m3_zenedge_accelerate) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, ptr);
    m3ApiGetArg(uint32_t, size);

    void *base = ivshmem_get_shared_memory();
    uint32_t shm_size = ivshmem_get_size();
    if (!base || shm_size == 0) {
        m3ApiReturn(0);
    }

    uintptr_t start = (uintptr_t)base;
    uintptr_t end = start + (uintptr_t)shm_size;
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t q = p + (uintptr_t)size;

    if (q < p) {
        m3ApiReturn(0);
    }
    if (p < start || q > end) {
        m3ApiReturn(0);
    }

    m3ApiReturn(1);
}
