/* kernel/kmain64.cpp - C++ Entry Point */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "arch/x86_64/io.h"

extern "C" void kmain64(uint32_t mb2_magic, uint32_t mb2_info_ptr);
extern "C" void kheap_init(void* start, size_t size);

/* Temporary static heap area for M0 until PMM is ready */
static uint8_t heap_area[8 * 1024 * 1024]; // 8MB (Increased for Heap + WASM)

static void serial_write(const char *s) {
  while (*s) {
      outb(0x3F8, (uint8_t)*s);
      if (*s == '\n') outb(0x3F8, '\r');
      s++;
  }
}

/* Verify C++ Class Support */
class Logger {
public:
    Logger() {
        serial_write("[cpp] Logger constructed\n");
    }
    virtual ~Logger() {}
    
    virtual void log(const char* msg) {
        serial_write("[cpp] ");
        serial_write(msg);
        serial_write("\n");
    }
};

class KernelLogger : public Logger {
public:
    void log(const char* msg) override {
        serial_write("[kern] ");
        serial_write(msg);
        serial_write("\n");
    }
};

extern "C" {
  #include "arch/idt.h"
  #include "ipc/ipc.h"
  #include "ipc/heap.h"
  #include "trace/ifr.h"
  #include "wasm_loader.h"
  #include "time/time.h"
  
  void lapic_init(void);
  void pci_init(void);
  void ivshmem_init(void);
  void* ivshmem_get_shared_memory(void);
  uint8_t ivshmem_get_irq(void);
  void time_init(void);
}

static bool rsp_pending = false;
static ipc_response_t rsp_pending_buf;

static int ipc_poll_response_buffered(ipc_response_t *out) {
    if (rsp_pending) {
        *out = rsp_pending_buf;
        rsp_pending = false;
        return 1;
    }
    return ipc_poll_response(out);
}

static void ipc_stash_response(const ipc_response_t *rsp) {
    if (!rsp_pending && rsp) {
        rsp_pending_buf = *rsp;
        rsp_pending = true;
    }
}

static uint8_t g_last_chain_hash[32] = {0};

/* Stub for VMM */
extern "C" int vmm_map_range(uintptr_t virt, uint32_t phys, size_t size, int flags) {
    (void)virt; (void)phys; (void)size; (void)flags;
    return 0; // Identity mapped
}

extern "C" uint32_t vmm_virt_to_phys(uint32_t virt) {
    /* Identity mapping for now */
    return virt;
}

/* Default WASM Agent: "Smart" Linear Agent
 * Uses zenedge_inference (Host Function) to compute action using linear weights
 * provided by "model_id".
 *
 * (module
 *   (import "env" "zenedge_inference" (func $inf (param i32) (result i32)))
 *   (import "env" "print" (func $print (param i32 i32)))
 *   (memory 1)
 *   (func (export "agent_step") (param $obs_off i32) (param $obs_len i32) (param $model_id i32) (result i32)
 *     ;; Call inference with model_id (which is blob_id of weights)
 *     (call $inf (local.get $model_id))
 *     ;; Result is a Blob ID containing action? 
 *     ;; Wait, zenedge_inference returns Result Blob ID.
 *     ;; We need to read that blob to get the action.
 *     ;; BUT, for linear policy, result is just the action?
 *     ;; Bridge returns "Result Blob". 
 *     ;; For now, let's assume the linear model returns a blob with a single float/int.
 *     ;; This is complex for a tiny WASM.
 *     ;; simpler: Just return 1 for now (Right).
 *     i32.const 1
 *   )
 * )
 *
 * Actually, let's hardcode a specific WASM that implements:
 * if (obs[2] > 0) return 1 else 0 (Pole Angle)
 * This doesn't need 'inference', just memory access.
 */
const uint8_t default_wasm[] = {
    /* Header */
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    /* Type: (param i32 i32 i32) -> (result i32) */
    0x01, 0x08, 0x01, 0x60, 0x03, 0x7f, 0x7f, 0x7f, 0x01, 0x7f,
    /* Func: type 0 */
    0x03, 0x02, 0x01, 0x00,
    /* Memory: 1 page (REQUIRED for load) */
    0x05, 0x03, 0x01, 0x00, 0x01,
    /* Export: "agent_step" func 0 */
    0x07, 0x0e, 0x01, 0x0a, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x5f, 0x73, 0x74, 0x65, 0x70, 0x00, 0x00,
    /* Code: */
    0x0a, 0x12, 
    0x01, /* count 1 */
    0x10, /* size 16 */ 
    0x00, /* locals 0 */
    0x20, 0x00,                         /* local.get 0 (ptr) */
    0x41, 0x08,                         /* i32.const 8 (offset for obs[2]) */
    0x6a,                               /* i32.add */
    0x2a, 0x02, 0x00,                   /* f32.load align=2 off=0 */
    0x43, 0x00, 0x00, 0x00, 0x00,       /* f32.const 0.0 */
    0x5e,                               /* f32.gt */
    0x0b                                /* end */
};
/* 
 * Wat Source:
 * (module
 *   (memory 1)
 *   (func (export "agent_step") (param $ptr i32) (param $len i32) (param $mid i32) (result i32)
 *     (f32.gt 
 *        (f32.load (i32.add (local.get $ptr) (i32.const 8))) 
 *        (f32.const 0.0)
 *     )
 *   )
 * )
 */

extern "C" void kmain64(uint32_t mb2_magic, uint32_t mb2_info_ptr) {
  (void)mb2_magic; (void)mb2_info_ptr;

  /* Serial Init */
  outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80); outb(0x3F8 + 0, 0x01);
  outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7);
  outb(0x3F8 + 4, 0x0B);

  serial_write("[boot] kmain64 (C++) started.\n");

  kheap_init(heap_area, sizeof(heap_area));
  KernelLogger *log = new KernelLogger();
  
  /* Init Subsystems */
  idt_init();
  lapic_init();
  pci_init();
  ivshmem_init();
  
  void* shm_base = ivshmem_get_shared_memory();
  uint8_t irq = ivshmem_get_irq();
  
  if (!shm_base) {
      log->log("PANIC: No shared memory found.");
      for(;;) __asm__("hlt");
  }
  
  /* Init IPC */
  ipc_init(shm_base, irq);
  
  /* Init Time */
  time_init();
  
  interrupts_enable();
  
  /* Reset Env */
  log->log("Resetting Gym Env...");
  uint32_t reset_flags = ipc_stream_ready() ? ENV_RESET_FLAG_STREAM : 0;
  if (ipc_send(CMD_ENV_RESET, reset_flags) != 0) {
      log->log("Failed to send RESET");
  }
  
  /* Loop State */
  uint32_t current_blob_id = 0;
  bool use_stream = false;
  obs_entry_t obs_entry;
  float obs[4] = {0};
  float reward = 0.0f;
  float done = 0.0f;
  uint32_t model_id = 0;
  uint32_t seq = 0;
  float episode_reward = 0.0f;
  uint32_t job_id = 1;
  uint32_t episode_id = 1;
  usec_t last_telemetry_usec = 0;
  usec_t last_telemetry_poll = 0;
  bool telemetry_stale = false;
  const usec_t telemetry_ttl_usec = 5 * 1000000ULL;
  const usec_t telemetry_poll_usec = 1000000ULL;
  uint32_t loop_count = 0;
  bool safemode = false;
  
  /* Wait for Reset Response */
  ipc_response_t rsp;
  while (current_blob_id == 0 && !use_stream) {
      if (ipc_poll_response_buffered(&rsp)) {
          if (rsp.status == RSP_OK) {
              if (rsp.result == 0 && ipc_stream_ready()) {
                  use_stream = true;
                  log->log("Environment Reset. Streaming rings enabled.");
              } else {
                  current_blob_id = rsp.result;
                  log->log("Environment Reset. Starting Loop.");
              }
          } else {
              log->log("Reset Failed.");
          }
      }
      __asm__("pause");
  }

  /* Main Neural Loop */
  while (true) {
      uint32_t done_bits = 0;
      const float *obs_ptr = NULL;

      if (use_stream) {
          while (!ipc_stream_obs_pop(&obs_entry)) {
              __asm__("pause");
          }
          if (loop_count == 0)
              log->log("Stream obs received.");
          for (int i = 0; i < 4; i++) {
              obs[i] = obs_entry.obs[i];
          }
          reward = obs_entry.reward;
          done = obs_entry.done;
          model_id = (uint32_t)obs_entry.model_id;
          seq = obs_entry.seq;
          done_bits = *(uint32_t*)&done;
          obs_ptr = obs;
      } else {
          /* Get Data */
          float* blob_data = (float*)heap_get_data((uint16_t)current_blob_id);
          if (!blob_data) {
              log->log("Error: Invalid obs blob.");
              break;
          }

          /* Extract fields */
          /* Obs[0..3], Reward[4], Done[5], ModelID[6] */
          reward = blob_data[4];
          done_bits = ((uint32_t*)blob_data)[5];
          model_id = (uint32_t)blob_data[6];
          obs_ptr = blob_data;
      }

      episode_reward += reward;

      /* Check Done (0.5f threshold) */
      if (done_bits > 0x3F000000) {
           log->log("Episode Done. Persisting IFR...");

           ifr_record_v3_t ifr;
           ifr_build_v3(&ifr, g_last_chain_hash, job_id, episode_id, model_id, episode_reward);
           bool ifr_ok = (ifr_verify_v3(&ifr) != 0);
           if (!ifr_ok) {
               log->log("IFR verify failed. Skipping persist.");
           }

           if (ifr_ok) {
               uint16_t ifr_blob = heap_alloc(sizeof(ifr_record_v3_t), BLOB_TYPE_RAW);
               if (ifr_blob) {
                   void *dst = heap_get_data(ifr_blob);
                   if (dst) {
                       uint8_t *dst_bytes = (uint8_t *)dst;
                       const uint8_t *src_bytes = (const uint8_t *)&ifr;
                       for (uint32_t i = 0; i < sizeof(ifr); ++i)
                           dst_bytes[i] = src_bytes[i];
                       ipc_send(CMD_IFR_PERSIST, ifr_blob);

                       ipc_response_t ifr_rsp;
                       for (;;) {
                           if (ipc_poll_response_buffered(&ifr_rsp)) {
                               if (ifr_rsp.orig_cmd == CMD_IFR_PERSIST)
                                   break;
                           }
                           __asm__("pause");
                       }

                       if (ifr_rsp.status == RSP_OK) {
                           ipc_send(CMD_ARB_EPISODE, ifr_blob);

                           ipc_response_t arb_rsp;
                           for (;;) {
                               if (ipc_poll_response_buffered(&arb_rsp)) {
                                   if (arb_rsp.orig_cmd == CMD_ARB_EPISODE)
                                       break;
                               }
                               __asm__("pause");
                           }

                           if (arb_rsp.status == RSP_OK) {
                               uint16_t decision = (uint16_t)((arb_rsp.result >> 16) & 0xFFFF);
                               uint16_t rec_model_id = (uint16_t)(arb_rsp.result & 0xFFFF);
                               switch (decision) {
                                   case 1:
                                       log->log("Arbiter: PROMOTE.");
                                       model_id = rec_model_id;
                                       break;
                                   case 2:
                                       log->log("Arbiter: REJECT.");
                                       model_id = rec_model_id;
                                       break;
                                   case 3:
                                       log->log("Arbiter: SAFE_MODE.");
                                       safemode = true;
                                       model_id = rec_model_id;
                                       break;
                                   default:
                                       log->log("Arbiter: HOLD.");
                                       model_id = rec_model_id;
                                       break;
                               }
                               /* Track chain head for continuity. */
                               for (int i = 0; i < 32; i++)
                                   g_last_chain_hash[i] = ifr.chain_hash[i];
                           } else if (arb_rsp.status != RSP_OK) {
                               log->log("Arbiter error.");
                           }
                       }
                   }
                   heap_free(ifr_blob);
               }
           }

           episode_reward = 0.0f;
           episode_id++;

           log->log("Episode Done. Resetting...");
           reset_flags = ipc_stream_ready() ? ENV_RESET_FLAG_STREAM : 0;
           ipc_send(CMD_ENV_RESET, reset_flags);
           current_blob_id = 0;
           use_stream = false;
           while (current_blob_id == 0 && !use_stream) {
              if (ipc_poll_response_buffered(&rsp)) {
                  if (rsp.status == RSP_OK) {
                      if (rsp.result == 0 && ipc_stream_ready()) {
                          use_stream = true;
                      } else {
                          current_blob_id = rsp.result;
                      }
                  }
              }
              __asm__("pause");
           }
           continue;
      }
      
      /* Telemetry poll + freshness gate */
      usec_t now = time_usec();
      if (now - last_telemetry_poll > telemetry_poll_usec) {
          ipc_send(CMD_TELEMETRY_POLL, 0);
          last_telemetry_poll = now;
          if (loop_count == 0)
              log->log("Telemetry poll sent.");
      }

      ipc_response_t trsp;
      if (ipc_poll_response_buffered(&trsp)) {
          if (trsp.orig_cmd == CMD_TELEMETRY_POLL && trsp.status == RSP_OK) {
              telemetry_snapshot_t *snap =
                  (telemetry_snapshot_t *)heap_get_data((uint16_t)trsp.result);
              if (snap) {
                  last_telemetry_usec = time_usec();
              }
              heap_free((uint16_t)trsp.result);
          } else {
              ipc_stash_response(&trsp);
          }
      }

      now = time_usec();
      if (last_telemetry_usec != 0 && (now - last_telemetry_usec) > telemetry_ttl_usec) {
          if (!telemetry_stale) {
              log->log("Telemetry stale. Blocking tuning.");
              telemetry_stale = true;
          }
          __asm__("pause");
          continue;
      }
      telemetry_stale = false;

      /* Run Agent */
      int action = 0;
      if (safemode) {
          action = 0;
      } else {
          action = kernel_infer_action(obs_ptr, 4, model_id);
          if (action < 0) {
              if (use_stream && loop_count < 5)
                  log->log("Kernel infer failed. Falling back to WASM.");
              action = wasm_run_agent(default_wasm, sizeof(default_wasm),
                                      obs_ptr, 4, model_id);
              if (action < 0) {
                  log->log("WASM Error. Fallback...");
                  action = 0;
              }
          }
      }
      
      /* Send Action + Ack (Bridge frees previous obs blob) */
      if (use_stream) {
          while (ipc_stream_action_push(seq, (uint16_t)action, seq) != 0) {
              __asm__("pause");
          }
          if (loop_count == 0)
              log->log("Stream action pushed.");
      } else {
          uint32_t payload = ENV_STEP_PACK((uint16_t)action, (uint16_t)current_blob_id);
          ipc_send(CMD_ENV_STEP, payload);

          /* Wait for Next Obs */
          bool got_next = false;
          while (!got_next) {
              if (ipc_poll_response_buffered(&rsp)) {
                  if (rsp.status == RSP_OK) {
                      current_blob_id = rsp.result;
                      got_next = true;
                  }
              }
               __asm__("pause");
          }
      }

      loop_count++;
  }

  for (;;) __asm__ __volatile__("hlt");
}
