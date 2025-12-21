/* kernel/wasm_loader.c */
#include <stdint.h>
#include <stddef.h>

#include "lib/wasm3/wasm3.h"
#include "lib/wasm3/m3_env.h"

#include "console.h"
#include "mm/kheap.h"
#include "ipc/ipc.h"
#include "ipc/ipc_proto.h"
#include "ipc/heap.h"
#include "lib/math.h"
#include "wasm/host_funcs.h"

#define WASM_STACK_SIZE        16384   // 16KB
#define WASM_PRINT_MAX_BYTES     512   // prevent console spam/DoS
#define WASM_MEMORY_LIMIT_PAGES   16   // 16 * 64KiB = 1MiB (tune via contracts later)
#define WASM_MEMORY_LIMIT_BYTES   (WASM_MEMORY_LIMIT_PAGES * 65536u)

static IM3Environment g_env = NULL;
static const float *g_last_obs = NULL;
static size_t g_last_obs_len = 0;
static uint32_t g_cached_model_id = 0;
static float *g_cached_weights = NULL;
static size_t g_cached_weights_len = 0;

static int wasm_load_model_weights(uint32_t model_id) {
    if (model_id == 0)
        return -1;
    if (g_cached_model_id == model_id && g_cached_weights && g_cached_weights_len > 0)
        return 0;

    uint32_t size = heap_get_blob_size((uint16_t)model_id);
    if (size == 0 || (size % sizeof(float)) != 0)
        return -1;

    size_t len = size / sizeof(float);
    const float *src = (const float *)heap_get_data((uint16_t)model_id);
    if (!src)
        return -1;

    float *buf = (float *)kmalloc(len * sizeof(float));
    if (!buf)
        return -1;

    for (size_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }

    if (g_cached_weights) {
        kfree(g_cached_weights);
    }

    g_cached_weights = buf;
    g_cached_weights_len = len;
    g_cached_model_id = model_id;
    return 0;
}

static int zenedge_infer_action(const float *obs_ptr, size_t obs_len, uint32_t model_id, int32_t *out_action) {
    if (!out_action || !obs_ptr || obs_len == 0)
        return -1;

    if (wasm_load_model_weights(model_id) != 0)
        return -1;

    size_t n = obs_len;
    if (g_cached_weights_len < n)
        n = g_cached_weights_len;
    if (n == 0)
        return -1;

    float score = math_vec_dot(obs_ptr, g_cached_weights, (int)n);
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = score;
    int is_positive = ((conv.u & 0x80000000u) == 0) && ((conv.u & 0x7FFFFFFFu) != 0);
    *out_action = is_positive ? 1 : 0;
    return 0;
}

// ---- helpers ----

static int wasm_mem_span_ok(IM3Runtime rt, const void* p, uint32_t len) {
    uint32_t mem_size = 0;
    uint8_t* mem = m3_GetMemory(rt, &mem_size, 0);  // one memory only; index must be 0
    if (!mem) return 0;

    uintptr_t base = (uintptr_t)mem;
    uintptr_t end  = base + (uintptr_t)mem_size;

    uintptr_t a = (uintptr_t)p;
    uintptr_t b = a + (uintptr_t)len;

    // overflow-safe range check
    if (b < a) return 0;
    if (a < base) return 0;
    if (b > end) return 0;
    return 1;
}

// ---- host imports ----

m3ApiRawFunction(m3_zenedge_inference);

m3ApiRawFunction(m3_zenedge_log_int) {
    m3ApiGetArg(int32_t, val);
    console_write("[wasm] log_int: ");
    print_uint((uint32_t)val);
    console_write("\n");
    m3ApiSuccess();
}

// env.print(ptr:i32, len:i32)
m3ApiRawFunction(m3_zenedge_print) {
    m3ApiGetArgMem(const uint8_t*, ptr);
    m3ApiGetArg(uint32_t, len);

    if (!runtime) m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    if (len > WASM_PRINT_MAX_BYTES) len = WASM_PRINT_MAX_BYTES;

    if (!wasm_mem_span_ok(runtime, ptr, len)) {
        console_write("[wasm] print OOB\n");
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }

    console_write("[wasm] ");
    // If you have console_putc/serial_char, use that. Otherwise chunk into a temp buffer.
    char tmp[64];
    uint32_t i = 0;
    while (i < len) {
        uint32_t n = (len - i);
        if (n > (uint32_t)(sizeof(tmp) - 1)) n = (uint32_t)(sizeof(tmp) - 1);

        for (uint32_t j = 0; j < n; j++) tmp[j] = (char)ptr[i + j];
        tmp[n] = 0;

        console_write(tmp);
        i += n;
    }
    console_write("\n");
    m3ApiSuccess();
}

// env.abort(msgPtr:i32, filePtr:i32, line:i32, col:i32)
m3ApiRawFunction(m3_zenedge_abort) {
    m3ApiGetArgMem(const uint8_t*, msg);
    m3ApiGetArgMem(const uint8_t*, file);
    m3ApiGetArg(uint32_t, line);
    m3ApiGetArg(uint32_t, col);

    console_write("[wasm] abort at ");
    print_uint(line);
    console_write(":");
    print_uint(col);
    console_write("\n");

    // Best effort: msg/file are pointers; we can’t assume NUL-terminated, so don’t print blindly.
    m3ApiTrap(m3Err_trapAbort);
}

// ---- public API ----

static int wasm_env_init_once(void) {
    if (g_env) return 0;

    console_write("[wasm] Initializing wasm3 environment...\n");
    g_env = m3_NewEnvironment();
    if (!g_env) {
        console_write("[wasm] m3_NewEnvironment failed\n");
        return -1;
    }
    return 0;
}

int wasm_run_bytes(const uint8_t* code, size_t size) {
    if (!code || size == 0) return -1;
    if (wasm_env_init_once() != 0) return -1;

    IM3Runtime rt = m3_NewRuntime(g_env, WASM_STACK_SIZE, NULL);
    if (!rt) {
        console_write("[wasm] m3_NewRuntime failed\n");
        return -1;
    }

    // Optional but highly recommended: cap linear memory (tie to your contract later).
    rt->memoryLimit = WASM_MEMORY_LIMIT_BYTES;

    IM3Module mod = NULL;
    M3Result res = m3_ParseModule(g_env, &mod, code, (uint32_t)size);
    if (res) {
        console_write("[wasm] Parse Error: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    res = m3_LoadModule(rt, mod);
    if (res) {
        console_write("[wasm] Load Error: ");
        console_write((char*)res);
        console_write("\n");
        // mod was not successfully loaded; safe to free it
        m3_FreeModule(mod);
        m3_FreeRuntime(rt);
        return -1;
    }
    // Do NOT free mod here; runtime owns it now

    // Link host functions
    m3_LinkRawFunction(mod, "env", "log_int", "v(i)",   &m3_zenedge_log_int);
    m3_LinkRawFunction(mod, "env", "print",   "v(*i)",  &m3_zenedge_print);
    m3_LinkRawFunction(mod, "env", "abort",   "v(**ii)", &m3_zenedge_abort);
    m3_LinkRawFunction(mod, "env", "zenedge_accelerate", "i(ii)", &m3_zenedge_accelerate);

    /* We need to forward declare or include the prototype if we link it here. 
       But wait, m3ApiRawFunction defines the function. 
       We should define zenedge_inference BEFORE wasm_run_bytes? 
       Or just declare it?
       m3_zenedge_inference is a function pointer name generated by macro?
       No, macro defines: const void * NAME (Runtime, etc).
       So we need a forward declaration if it's below.
    */
    /* But standard C requires definition or decl. 
       Let's move zenedge_inference definition ABOVE wasm_run_bytes in next step?
       Or just decl it here?
    */
    /* Hack: Just don't link inference in run_bytes for now? 
       run_bytes is for simple tests.
       zenedge_inference is for AGENTS.
    */
    
    IM3Function f = NULL;
    res = m3_FindFunction(&f, rt, "start");
    if (res) res = m3_FindFunction(&f, rt, "_start");
    
    if (res) {
        console_write("[wasm] FindFunction failed: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    // console_write("[wasm] Running...\n");
    res = m3_CallV(f);
    if (res) {
        console_write("[wasm] Run Error: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    // console_write("[wasm] Execution Complete.\n");
    m3_FreeRuntime(rt);
    return 0;
}

// env.zenedge_inference(tensor_id: i32) -> result_id: i32
m3ApiRawFunction(m3_zenedge_inference) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, tensor_id);
    
    if (!g_last_obs || g_last_obs_len == 0) {
        m3ApiReturn(0);
    }

    int32_t action = 0;
    if (zenedge_infer_action(g_last_obs, g_last_obs_len, (uint32_t)tensor_id, &action) != 0)
        m3ApiReturn(0);

    m3ApiReturn(action);
}

int wasm_run_agent(const uint8_t* code, size_t size, const float* obs_ptr, size_t obs_len, uint32_t model_id) {
    // console_write("[wasm] run_agent start\n");
    if (!code || size == 0) return -1;
    if (wasm_env_init_once() != 0) return -1;

    IM3Runtime rt = m3_NewRuntime(g_env, WASM_STACK_SIZE, NULL);
    if (!rt) return -1;
    rt->memoryLimit = WASM_MEMORY_LIMIT_BYTES;

    IM3Module mod = NULL;
    // console_write("[wasm] Parsing...\n");
    M3Result res = m3_ParseModule(g_env, &mod, code, (uint32_t)size);
    if (res) {
        console_write("[wasm] Parse failed: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }
    
    res = m3_LoadModule(rt, mod);
    if (res) {
        console_write("[wasm] Load failed: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    /* Link Host Functions */
    // console_write("[wasm] Linking...\n");
    m3_LinkRawFunction(mod, "env", "log_int", "v(i)",   &m3_zenedge_log_int);
    m3_LinkRawFunction(mod, "env", "print",   "v(*i)",  &m3_zenedge_print);
    m3_LinkRawFunction(mod, "env", "abort",   "v(**ii)", &m3_zenedge_abort);
    m3_LinkRawFunction(mod, "env", "zenedge_inference", "i(i)", &m3_zenedge_inference);
    m3_LinkRawFunction(mod, "env", "zenedge_accelerate", "i(ii)", &m3_zenedge_accelerate);

    IM3Function f = NULL;
    M3Result find_res = m3_FindFunction(&f, rt, "agent_step");
    if (find_res) {
        console_write("[wasm] agent_step not found: ");
        console_write((char*)find_res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    /* Allocate Input Buffer in WASM Memory */
    uint32_t input_offset = 1024; 
    uint32_t mem_size = 0;
    uint8_t* mem = m3_GetMemory(rt, &mem_size, 0);
    if (!mem || mem_size < input_offset + (obs_len * sizeof(float))) {
         console_write("[wasm] OOM for input\n");
         m3_FreeRuntime(rt);
         return -1;
    }
    
    /* Copy Obs */
    for (size_t i = 0; i < obs_len; i++) {
        ((float*)(mem + input_offset))[i] = obs_ptr[i];
    }
    
    g_last_obs = obs_ptr;
    g_last_obs_len = obs_len;

    /* Call agent_step(offset, len, model_id) */
    // console_write("[wasm] Calling agent_step...\n");
    res = m3_CallV(f, input_offset, (uint32_t)obs_len, model_id);
    
    if (res) {
        console_write("[wasm] agent step error: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }
    
    /* Get Return Value */
    uint32_t action = 0;
    m3_GetResultsV(f, &action);
    
    // console_write("[wasm] Done.\n");
    m3_FreeRuntime(rt);
    return (int)action;
}

int kernel_infer_action(const float *obs_ptr, size_t obs_len, uint32_t model_id) {
    int32_t action = 0;
    if (zenedge_infer_action(obs_ptr, obs_len, model_id, &action) != 0)
        return -1;
    return (int)action;
}

const float* wasm_get_profile(uint32_t *model_id, uint16_t *len) {
    if (model_id)
        *model_id = g_cached_model_id;
    if (len) {
        uint16_t capped = (g_cached_weights_len > 0xFFFFu) ? 0xFFFFu : (uint16_t)g_cached_weights_len;
        *len = capped;
    }
    return g_cached_weights;
}
