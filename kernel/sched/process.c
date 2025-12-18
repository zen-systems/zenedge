/* kernel/sched/process.c */
#include "../process.h"
#include "../console.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../arch/gdt.h"
#include "sched_core.h"
#include "../include/string.h"

extern void enter_user_mode(void *entry_point, void *user_stack);

/* Global list (for now, simplistic) */
extern process_t *process_list;

process_t *sched_create_user_process(uint32_t entry_point, uint32_t wasm_blob_phys, uint32_t wasm_size) {
    /* 1. Allocate Process Struct (PCB) */
    paddr_t proc_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
    if (!proc_phys) return NULL;
    
    process_t *proc = (process_t *)phys_to_virt(proc_phys);
    memset(proc, 0, sizeof(process_t));
    
    // Assign generic PID (1..N)
    static uint32_t pid_counter = 1;
    proc->pid = pid_counter++;
    proc->state = PROCESS_STATE_NEW;
    
    /* 2. Create Page Directory */
    proc->cr3 = vmm_create_user_pd();
    if (!proc->cr3) {
        pmm_free_page(proc_phys);
        return NULL;
    }
    proc->pd_virt = (uint32_t *)phys_to_virt(proc->cr3);

    /* 3. Allocate Kernel Stack (4KB) */
    paddr_t kstack_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
    if (!kstack_phys) {
        vmm_destroy_user_pd(proc->cr3);
        pmm_free_page(proc_phys);
        return NULL;
    }
    proc->kstack_top = (uint32_t)phys_to_virt(kstack_phys) + 4096;

    /* 4. Map User Stack/Heap (Minimal setup for now) */
    /* Stack at 0xBFFFF000 (just below kernel) */
    /* We'll just map one page for stack for now */
    paddr_t current_pd = vmm_get_current_pd();
    vmm_switch_pd(proc->cr3);
    
    // Process-specific mappings would happen here
    // For now we assume the caller/loader will handle mapping code/data
    
    vmm_switch_pd(current_pd);


    /* 5. Setup Trampoline for Context Switch */
    /* When switch_to switches TO this process, it will pop registers and 'ret'.
     * We want it to 'ret' into 'enter_user_mode'.
     *
     * Stack Layout at proc->esp:
     * [ ... ]
     * [Arg 2   ] User Stack Top (0xBFFFF000)
     * [Arg 1   ] Entry Point
     * [RetAddr ] 0 (Fake return for enter_user_mode)
     * [RetAddr ] enter_user_mode (where switch_to returns to)
     * [EBP     ] 0
     * [EBX     ] 0
     * [ESI     ] 0
     * [EDI     ] 0
     */
     
    uint32_t *sp = (uint32_t *)proc->kstack_top;
    
    /* Push Args for enter_user_mode */
    *(--sp) = 0xBFFFF000;    /* User Stack */
    *(--sp) = entry_point;   /* Entry Point */
    *(--sp) = 0;             /* Fake Return */
    *(--sp) = (uint32_t)enter_user_mode;
    
    /* Push Callee-Saved Regs (popped by switch_to) */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EDI */
    
    proc->esp = (uint32_t)sp;
    
    /* 6. Default Contract */
    proc->mem_pages_limit = 256; /* 1MB limit */
    proc->quantum_ms = 50;
    proc->ticks_remaining = 5;

    console_write("[proc] created pid=");
    print_uint(proc->pid);
    console_write(" cr3=");
    print_hex32(proc->cr3);
    console_write("\n");

    return proc;
}

void sched_destroy_process(process_t *proc) {
    if (!proc) return;
    
    console_write("[proc] destroying pid=");
    print_uint(proc->pid);
    console_write("\n");

    /* Free Kernel Stack */
    if (proc->kstack_top) {
        paddr_t kstack_phys = virt_to_phys(proc->kstack_top - 4096);
        pmm_free_page(kstack_phys);
    }
    
    /* Free Page Directory */
    if (proc->cr3) {
        vmm_destroy_user_pd(proc->cr3);
    }
    
    /* Free PCB */
    paddr_t proc_phys = virt_to_phys((vaddr_t)proc);
    pmm_free_page(proc_phys);
}
