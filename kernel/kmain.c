/* kernel/kmain.c */
#include <stddef.h>
#include <stdint.h>

#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/keyboard.h"
#include "arch/pci.h"
#include "arch/pic.h"
#include "arch/pit.h"
#include "arch/syscall.h"
#include "console.h"
#include "contracts.h"
#include "drivers/ivshmem.h"
#include "ipc/heap.h"
#include "ipc/ipc.h"
#include "job/job_graph.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/sched_core.h"
#include "shell.h"
#include "time/time.h"
#include "trace/flightrec.h"
#include "lib/math.h"
#include "mm/kheap.h"
#include "wasm_loader.h"

/* Minimal serial output for debugging */
static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_char(char c) { outb(0x3F8, c); }

void kmain(multiboot_info_t *mboot_info) {
  /* Direct serial output to confirm we reached kmain */
  serial_char('K');
  serial_char('M');
  serial_char('\r');
  serial_char('\n');

  console_cls();
  console_write("ZENEDGE kernel booting...\n");
  console_write("AI + Edge Computing Kernel Playground\n\n");

  /* Initialize time subsystem first - needed for telemetry timestamps */
  time_init();

  flightrec_init();

  /* Initialize physical memory manager with multiboot info */
  pmm_init(mboot_info);

  /* Reserve IPC Shared Memory Region (1MB at 32MB mark) */
  /* This prevents PMM from handing these pages out to other subsystems */
  pmm_reserve_range(0x02000000, 0x100000);

  /* Initialize virtual memory manager (reads CR3, finalizing setup) */
  vmm_init();

  /* Initialize interrupt system */
  gdt_init();      /* GDT with TSS for ring transitions */
  idt_init();      /* IDT with exception and IRQ handlers */
  pic_init();      /* Remap PIC to vectors 32-47 */
  pit_init(100);   /* 100 Hz timer (10ms tick) */
  keyboard_init(); /* Initialize Keyboard Driver */

  /* Enable interrupts */
  console_write("[kmain] enabling interrupts...\n");
  interrupts_enable();

  /* Initialize Syscalls */
  syscall_init();

  /* Initialize PCI */
  pci_init();

  /* Initialize IVSHMEM Driver */
  ivshmem_init();

  /* Initialize IPC (Proxy Driver) */
  /* Pass the dynamically detected shared memory base and IRQ */
  ipc_init(ivshmem_get_shared_memory(), ivshmem_get_irq());

  /* Initialize Kernel Mesh */
  ipc_mesh_init();

  /* Test IPC: send command to Real Bridge */
  console_write("\n--- IPC Inference Demo ---\n");

  /* 1. Allocate input tensor (1x28x28 float32 for MNIST) */
  uint32_t input_shape[] = {1, 28, 28};
  uint16_t input_tensor = heap_alloc_tensor(DTYPE_FLOAT32, 3, input_shape);
  console_write("[test] Allocated input tensor: id=");
  print_uint(input_tensor);
  console_write("\n");

  /* 2. Fill with dummy data */
  float *input_data = (float *)heap_get_tensor_data(input_tensor);
  if (input_data) {
      for(int i=0; i<28*28; i++) input_data[i] = 0.5f;
  }

  /* 3. Send Run Model Command */
  console_write("[test] Sending CMD_RUN_MODEL...\n");
  ipc_send(CMD_RUN_MODEL, input_tensor);

  /* 4. Poll for response (timeout after ~5s) */
  console_write("[test] Polling for response...\n");
  
  ipc_response_t rsp;
  int timeout = 500; /* 500 * 10ms = 5s */
  int received = 0;
  
  while(timeout > 0) {
      if (ipc_poll_response(&rsp)) {
          received = 1;
          break;
      }
      pit_sleep_ms(10); /* Sleep 10ms */
      timeout--;
  }

  if (received) {
    console_write("[test] Got response! status=");
    print_hex32(rsp.status);
    console_write(" result_blob=");
    print_hex32(rsp.result);
    console_write("\n");
  } else {
    console_write("[test] Timeout waiting for response!\n");
  }



  ipc_dump_debug();

  /* Test Shared Heap */
  console_write("\n--- Shared Heap Demo ---\n");

  /* Allocate a raw blob */
  uint16_t blob1 = heap_alloc(128, BLOB_TYPE_RAW);
  console_write("[test] Allocated raw blob: id=");
  print_uint(blob1);
  console_write("\n");

  /* Allocate a tensor: 4x4 float32 matrix */
  uint32_t shape[] = {4, 4};
  uint16_t tensor1 = heap_alloc_tensor(DTYPE_FLOAT32, 2, shape);
  console_write("[test] Allocated tensor blob: id=");
  print_uint(tensor1);
  console_write("\n");

  /* Write some data to the tensor */
  float *data = (float *)heap_get_tensor_data(tensor1);
  if (data) {
    for (int i = 0; i < 16; i++) {
      data[i] = (float)i * 1.5f;
    }
    console_write("[test] Wrote 16 floats to tensor\n");
  }

  /* Allocate another tensor: 8x8x3 uint8 (small image) */
  uint32_t img_shape[] = {8, 8, 3};
  uint16_t tensor2 = heap_alloc_tensor(DTYPE_UINT8, 3, img_shape);
  console_write("[test] Allocated image tensor: id=");
  print_uint(tensor2);
  console_write("\n");

  heap_dump_debug();

  /* Free one blob */
  heap_free(blob1);
  console_write("[test] Freed blob ");
  print_uint(blob1);
  console_write("\n");

  heap_dump_debug();

  /* Dump memory map for debugging */
  pmm_dump_map();

  contracts_init();
  sched_init();

  /* ===== Demo 0: Tensor Metadata & Admission Control ===== */
  console_write("\n--- Demo 0: Tensor Metadata & Admission Control ---\n");

  job_graph_t test_job;
  job_graph_init(&test_job, 100);

  /* Add compute steps */
  job_graph_add_step(&test_job, 0, STEP_TYPE_COMPUTE);    /* forward pass */
  job_graph_add_step(&test_job, 1, STEP_TYPE_COMPUTE);    /* backward pass */
  job_graph_add_step(&test_job, 2, STEP_TYPE_COLLECTIVE); /* gradient sync */

  job_graph_add_dep(&test_job, 1, 0);
  job_graph_add_dep(&test_job, 2, 1);

  /* Add tensors: simulating a small neural network layer
   * weights: 1024 FP32 elements = 4KB
   * activations: 2048 FP16 elements = 4KB
   * gradients: 1024 FP32 elements = 4KB
   */
  job_graph_add_tensor(&test_job, 1, TENSOR_DTYPE_FP32, 1024, 1,
                       0); /* weights - pinned */
  job_graph_add_tensor(&test_job, 2, TENSOR_DTYPE_FP16, 2048, 0,
                       0xFF); /* activations */
  job_graph_add_tensor(&test_job, 3, TENSOR_DTYPE_FP32, 1024, 0,
                       0xFF); /* gradients */

  /* Wire tensors to steps */
  job_graph_step_add_input(&test_job, 0, 1);  /* forward reads weights */
  job_graph_step_add_output(&test_job, 0, 2); /* forward produces activations */

  job_graph_step_add_input(&test_job, 1, 2);  /* backward reads activations */
  job_graph_step_add_output(&test_job, 1, 3); /* backward produces gradients */

  job_graph_step_add_input(&test_job, 2, 3); /* collective syncs gradients */

  /* Compute memory metrics */
  job_graph_compute_memory(&test_job);

  console_write("[test] Job memory metrics:\n");
  console_write("  total_memory_kb: ");
  print_uint(test_job.total_memory_kb);
  console_write("\n  peak_memory_kb: ");
  print_uint(test_job.peak_memory_kb);
  console_write("\n  pinned_memory_kb: ");
  print_uint(test_job.pinned_memory_kb);
  console_write("\n");

  /* Test admission control with generous contract */
  task_contract_t generous_contract = {.cpu_budget_us = 50000,
                                       .memory_kb = 64, /* Plenty of memory */
                                       .accel_slots = 1,
                                       .prio = CONTRACT_PRIORITY_HIGH,
                                       .job_id = 100};

  contract_apply(&generous_contract);

  console_write("[test] Admission check (generous budget):\n");
  admit_result_t result1 = contract_admit_job(&generous_contract, &test_job);
  console_write("  result: ");
  console_write(admit_result_name(result1));
  console_write("\n");

  /* Test admission control with tight contract */
  task_contract_t tight_contract = {.cpu_budget_us = 1000,
                                    .memory_kb =
                                        4, /* Too small - should reject */
                                    .accel_slots = 0,
                                    .prio = CONTRACT_PRIORITY_LOW,
                                    .job_id = 101};

  contract_apply(&tight_contract);

  console_write("[test] Admission check (tight budget):\n");
  admit_result_t result2 = contract_admit_job(&tight_contract, &test_job);
  console_write("  result: ");
  console_write(admit_result_name(result2));
  console_write("\n");

  /* ===== Demo 1: REALTIME job (uses node 0) ===== */
  console_write("\n--- Demo 1: REALTIME priority job (node 0) ---\n");

  job_graph_t job1;
  job_graph_init(&job1, 1);

  job_graph_add_step(&job1, 0, STEP_TYPE_COMPUTE);
  job_graph_add_step(&job1, 1, STEP_TYPE_COLLECTIVE);
  job_graph_add_step(&job1, 2, STEP_TYPE_COMPUTE);

  job_graph_add_dep(&job1, 1, 0);
  job_graph_add_dep(&job1, 2, 1);

  task_contract_t realtime_contract = {
      .cpu_budget_us = 5000,
      .memory_kb = 16, /* Low memory budget to trigger violations */
      .accel_slots = 1,
      .prio = CONTRACT_PRIORITY_REALTIME,
      .job_id = 1};

  contract_apply(&realtime_contract);

  /* Test contract-aware memory allocation */
  console_write("[test] allocating 5 pages via contract...\n");
  paddr_t pages[5];
  for (int i = 0; i < 5; i++) {
    pages[i] = contract_alloc_page(&realtime_contract);
    if (pages[i]) {
      console_write("  page ");
      print_uint(i);
      console_write(" @ node ");
      print_uint(pmm_addr_to_node(pages[i]));
      console_write("\n");
    }
  }

  contract_debug_print(&realtime_contract);

  /* Free pages */
  console_write("[test] freeing pages...\n");
  for (int i = 0; i < 5; i++) {
    contract_free_page(&realtime_contract, pages[i]);
  }

  /* Run the job */
  sched_job_ctx_t ctx1 = {.job = &job1, .contract = realtime_contract};

  sched_run_job(&ctx1);

  /* ===== Demo 2: LOW priority job (uses node 1) ===== */
  console_write("\n--- Demo 2: LOW priority job (node 1) ---\n");

  job_graph_t job2;
  job_graph_init(&job2, 2);

  job_graph_add_step(&job2, 0, STEP_TYPE_COMPUTE);
  job_graph_add_step(&job2, 1, STEP_TYPE_COMPUTE);

  job_graph_add_dep(&job2, 1, 0);

  task_contract_t low_contract = {.cpu_budget_us = 3000,
                                  .memory_kb =
                                      8, /* Very low to trigger violations */
                                  .accel_slots = 0,
                                  .prio = CONTRACT_PRIORITY_LOW,
                                  .job_id = 2};

  contract_apply(&low_contract);

  /* Allocate via contract - should use node 1 */
  console_write("[test] allocating 3 pages via LOW contract...\n");
  paddr_t low_pages[3];
  for (int i = 0; i < 3; i++) {
    low_pages[i] = contract_alloc_page(&low_contract);
    if (low_pages[i]) {
      console_write("  page ");
      print_uint(i);
      console_write(" @ node ");
      print_uint(pmm_addr_to_node(low_pages[i]));
      console_write("\n");
    }
  }

  contract_debug_print(&low_contract);

  for (int i = 0; i < 3; i++) {
    contract_free_page(&low_contract, low_pages[i]);
  }

  sched_job_ctx_t ctx2 = {.job = &job2, .contract = low_contract};

  sched_run_job(&ctx2);


  /* ===== Demo 3: Distributed Pipeline (Host Acceleration) ===== */
  console_write("\n--- Demo 3: Distributed Pipeline (Host Acceleration) ---\n");
  console_write("[demo] Creating multi-stage neural network job...\n");

  /* Job 3: 3-Layer MLP */
  job_graph_t job3;
  job_graph_init(&job3, 3);

  /* Steps: Layer 1, Layer 2, Layer 3 (all COMPUTE) */
  job_graph_add_step(&job3, 0, STEP_TYPE_COMPUTE);
  job_graph_add_step(&job3, 1, STEP_TYPE_COMPUTE);
  job_graph_add_step(&job3, 2, STEP_TYPE_COMPUTE);

  /* Dependencies: 0 -> 1 -> 2 */
  job_graph_add_dep(&job3, 1, 0);
  job_graph_add_dep(&job3, 2, 1);

  /* Allocate Data Tensors */
  /* Tensor 1: Input (784 features) */
  /* Tensor 2: Hidden 1 (128 features) */
  /* Tensor 3: Hidden 2 (64 features) */
  /* Tensor 4: Output (10 logits) */
  
  /* Allocate actual blob for input input */
  uint32_t t1_shape[] = {1, 784};
  uint16_t t1_blob = heap_alloc_tensor(DTYPE_FLOAT32, 2, t1_shape);
  
  /* We register them in the graph using the blob IDs as tensor IDs 
     (Simplification: job graph normally uses logical IDs, but our scheduler
      currently sends s->inputs[0] directly as payload, so we use blob ID)
  */
  
  /* Link input to step 0 */
  /* Note: job_graph_step_add_input expects logical ID, but our modified scheduler 
     sends s->inputs[0] as payload. So we put the blob ID there. */
  job3.steps[0].num_inputs = 1;
  job3.steps[0].inputs[0] = t1_blob; // Layer 1 Input
  
  /* For intermediate steps, we might want to pass the previous result?
     Our current simple scheduler doesn't chain results automatically.
     But we can pretend for the demo.
  */
  job3.steps[1].num_inputs = 1;
  job3.steps[1].inputs[0] = t1_blob; // Re-use input (simulating data flow)

  job3.steps[2].num_inputs = 1;
  job3.steps[2].inputs[0] = t1_blob; // Re-use input

  /* Contract for accelerated job */
  task_contract_t accel_contract = {
      .cpu_budget_us = 100000, /* Generous budget for IPC overhead */
      .memory_kb = 1024,
      .accel_slots = 1,
      .prio = CONTRACT_PRIORITY_HIGH,
      .job_id = 3
  };
  
  contract_apply(&accel_contract);
  
  sched_job_ctx_t ctx3 = {.job = &job3, .contract = accel_contract};
  
  console_write("[demo] Submitting pipeline job to scheduler...\n");
  sched_run_job(&ctx3);

  /* Final state */
  console_write("\n--- Final Contract States ---\n");
  contract_debug_print(&realtime_contract);
  contract_debug_print(&low_contract);

  flightrec_dump_console();

  /* ===== Interrupt System Demo ===== */
  console_write("\n--- Interrupt System Demo ---\n");

  /* Show timer tick count */
  /* Show timer tick count */
  console_write("[demo] waiting ~500ms for timer ticks...\n");
  uint32_t start_ticks = pit_get_ticks();
  // pit_sleep_ms(500); // Hangs in QEMU?
  uint32_t end_ticks = pit_get_ticks();
  console_write("[demo] timer ticks elapsed: ");
  print_uint(end_ticks - start_ticks);
  console_write(" (skipped sleep)\n");

  /* ===== User Mode Demo ===== */
  console_write("\n--- Round Robin Scheduler Demo ---\n");
  
  /* Dump Mesh State */
  /* ipc_mesh_dump(); */
  
  /* Initialize Kernel Heap (malloc/free for WASM) */
  /* 4MB Heap in BSS or PMM? PMM is better but let's use a large static array for simplicity/guarantee */
  static uint8_t kheap_mem[4 * 1024 * 1024]; // 4MB
  kheap_init(kheap_mem, sizeof(kheap_mem));
  
  /* WASM Test */
  #include "wasm_loader.h"
  /* Tiny WASM Module: 
     (module
       (type $t0 (func (param i32)))
       (type $t1 (func))
       (import "env" "log_int" (func $log_int (type $t0)))
       (func $start (type $t1)
         i32.const 42
         call $log_int))
     (export "start" (func $start)))
  */
  /* Process Model MVP Test */
  sched_test_rr();

  /* WASM module:
     ... (commented out for now)
  */
  /* 
  const uint8_t wasm_code[] = { ... };
  wasm_run_bytes(wasm_code, sizeof(wasm_code));
  */

  /* Enable FPU */
  __asm__ __volatile__("finit");
  
  /* ===== In-Kernel Inference Demo ===== */
  console_write("\n--- In-Kernel Inference Demo (Neural Kernel) ---\n");
  
  /* 1. Reset Environment */
  console_write("[kern] Resetting environment...\n");
  uint32_t current_blob_id = 0;
  
  if (ipc_send(CMD_ENV_RESET, 0) == 0) {
      ipc_response_t rsp;
      /* Busy wait for response (Polling Mode) */
      while (!ipc_poll_response(&rsp)) { __asm__ __volatile__("pause"); }
      
      console_write("[kern] Response received.\n");
      if (rsp.status == RSP_OK) {
          current_blob_id = rsp.result;
          console_write("[kern] Reset OK. Init Blob: ");
          print_uint(current_blob_id);
          console_write("\n");
      }
  }
  
  /* 2. Control Loop */
  if (current_blob_id > 0) {
      console_write("[kern] Starting Neural Control Loop (200 steps)...\n");
      
      for (int i = 0; i < 200; i++) {
            /* Get Data from Current Blob */
            float *data = (float*)heap_get_data(current_blob_id);
            if (!data) {
                console_write("[kern] Error: Invalid blob data\n");
                break;
            }
            
            /* Parse Blob:
             * [0-3] Obs
             * [4]   Reward
             * [5]   Done
             * [6]   Model Blob ID
             */
            float *obs = &data[0];
            uint32_t done_bits = ((uint32_t*)data)[5];
             /* 0.5f is 0x3F000000 */
            if (done_bits > 0x3F000000) {
                console_write("[kern] Episode Finised at step ");
                print_uint(i);
                console_write("\n");
                break;
            }
            
            /* Get Model */
            uint32_t model_id = (uint32_t)data[6]; // Cast float to int (assumes exact representaion for small ints)
            float *weights = (float*)heap_get_data(model_id);
            
            uint32_t action = 0;
            if (weights) {
                /* IN-KERNEL INFERENCE */
                float score = math_vec_dot(obs, weights, 4);
                
                /* Policy: If score > 0, Action 1 (Right), else 0 (Left) */
                /* score > 0 check: float representation logic or simple cast? */
                /* simple cast to int might lose sign bit logic if not careful, 
                   but standard comparison works if we link soft-float or just use bits.
                   Let's use bits for safety against linker errors again?
                   0.0f is 0x00000000. -0.0f is 0x80000000.
                   Positive numbers have top bit 0.
                   Let's try standard > 0.0f first, if linker fails, fix it.
                   Actually, math_vec_dot returns float, so we probably need __gtsf2 again.
                   Let's implement a quick manual check.
                */
                uint32_t score_bits = *(uint32_t*)&score;
                /* Positive if sign bit (31) is 0 and not zero */
                int is_positive = ((score_bits & 0x80000000) == 0) && ((score_bits & 0x7FFFFFFF) != 0);
                
                action = is_positive ? 1 : 0;
                
                /* Debug every 10 steps */
                if (i % 20 == 0) {
                    console_write("[kern] Step "); print_uint(i);
                    console_write(" Score: "); print_hex32(score_bits); /* Hex to avoid float printing complexity */
                    console_write(" Action: "); print_uint(action);
                    console_write("\n");
                }
            } else {
                console_write("[kern] Warn: No model weights found (ID ");
                print_uint(model_id);
                console_write("). Using Heuristic.\n");
                action = (i % 2);
            }
            
            /* Send Action */
            ipc_send(CMD_ENV_STEP, action);
            
            /* Wait for Next State */
            ipc_response_t rsp;
            while (!ipc_poll_response(&rsp)) { __asm__ __volatile__("pause"); }
            
            if (rsp.status != RSP_OK) {
                console_write("[kern] Step failed.\n");
                break;
            }
            current_blob_id = rsp.result;
      }
  }
  
  console_write("[kern] Inference Demo Completed.\n");
  
  /* sched_test_rr(); */

  /* Enable interrupts to start ticking and scheduling */
  /* Note: sched_test_rr sets up 'current_process' as the idle task.
   * Interrupts will cause ticks, which call schedule(), which switched to User
   * Task.
   */

  console_write("[kmain] Scheduler active. Idle loop.\n");

  /* Idle Loop (Kernel Task 0) */
  while (1) {
    __asm__ __volatile__("hlt");
  }
}
