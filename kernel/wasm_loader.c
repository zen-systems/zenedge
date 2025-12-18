/* kernel/wasm_loader.c */
#include <stdint.h>
#include <stddef.h>

#include "lib/wasm3/wasm3.h"
#include "lib/wasm3/m3_env.h"

#include "console.h"
#include "mm/kheap.h"

#define WASM_STACK_SIZE        16384   // 16KB
#define WASM_PRINT_MAX_BYTES     512   // prevent console spam/DoS
#define WASM_MEMORY_LIMIT_PAGES   16   // 16 * 64KiB = 1MiB (tune via contracts later)

static IM3Environment g_env = NULL;

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
    rt->memoryLimit = WASM_MEMORY_LIMIT_PAGES;

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
    m3_LinkRawFunction(mod, "env", "print",   "v(*i)",  &m3_zenedge_print);  // ptr + len
    m3_LinkRawFunction(mod, "env", "abort",   "v(**ii)", &m3_zenedge_abort); // best-effort signature

    IM3Function f = NULL;
    res = m3_FindFunction(&f, rt, "start");
    if (res) res = m3_FindFunction(&f, rt, "_start");  // common convention
    if (res) {
        console_write("[wasm] FindFunction failed: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    console_write("[wasm] Running...\n");
    res = m3_CallV(f);
    if (res) {
        console_write("[wasm] Run Error: ");
        console_write((char*)res);
        console_write("\n");
        m3_FreeRuntime(rt);
        return -1;
    }

    console_write("[wasm] Execution Complete.\n");
    m3_FreeRuntime(rt);
    return 0;
}
