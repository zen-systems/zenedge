# Architecture: Proxy Driver (Sidecar Model)

## Problem
Native driver support for high-end AI accelerators (NVIDIA GSP, Intel Userspace runtimes) is impossible or impractical to implement natively in a custom kernel like ZENEDGE.

## Solution
Use a **Linux Sidecar** model. ZENEDGE runs on dedicated cores (or a VM) and communicates with a Linux host that holds the actual GPU drivers (CUDA/OneAPI) via shared memory.

## Architecture
**Hardware**: AMP (Asymmetric Multi-Processing) setup.
- **Core 0-1 (Linux)**: Runs official NVIDIA/Intel Drivers + "Bridge Daemon".
- **Core 2-3 (ZENEDGE)**: Runs RTOS logic + "Proxy Driver".

### The Interaction
1. **User Process (ZENEDGE)**: Calls `contract_submit_job(model_id)`.
2. **ZENEDGE Kernel**: Validates Contract (Quota/QoS).
3. **Proxy Driver**:
   - Writes `CMD_EXECUTE_MODEL` to **Shared Memory Ring Buffer**.
   - Rings "Doorbell" (Inter-Processor Interrupt or Polling).
4. **Linux Bridge Daemon**:
   - Wakes up.
   - Reads Command.
   - Executes CUDA/OpenCL kernel.
   - Writes Result/Status back to Shared Memory.
   - Rings Doorbell back.
5. **ZENEDGE Kernel**:
   - Receives completion interrupt.
   - Updates Job status.
   - Wakes up User Process.

## Implementation Details (Phase 4)

### Shared Memory Structure
```c
typedef struct {
    uint32_t magic;      // 0x51DECA9E (SIDEAR)
    uint32_t head;       // Written by Producer (ZENEDGE)
    uint32_t tail;       // Written by Consumer (Linux)
    uint32_t size;       // Size of ring buffer
    uint8_t  data[];     // Command packets
} ipc_ring_t;

typedef struct {
    uint16_t cmd;        // CMD_RUN_MODEL, CMD_PING, etc.
    uint16_t flags;
    uint32_t payload_id; // ID of model/data in shared heap
    uint64_t timestamp;  // For latency tracking
} ipc_packet_t;
```

### Roadmap
1.  **Phase 4.1**: Define Protocol structs (`ipc_proto.h`).
2.  **Phase 4.2**: Implement `ipc_send()` in ZENEDGE.
3.  **Phase 4.3**: Simulate "Bridge" (initially a loopback or mocked memory region in QEMU).
