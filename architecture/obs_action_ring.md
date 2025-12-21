# Streaming Obs/Action Rings (V2)

## Motivation
- Eliminate per-step command/response chatter for the control loop.
- Keep Kernel as master and Host as peripheral.
- Avoid per-step heap allocation by streaming fixed-size records.

## Proposed Shared Memory Layout
Reserve the tail of the shared memory region for two rings.
This avoids moving the heap base and keeps command/response rings intact.

- 0x00000 - 0x07FFF: Command ring (unchanged)
- 0x08000 - 0x0FFFF: Response ring (unchanged)
- 0x10000 - 0x100FF: Doorbell control (unchanged)
- 0x10100 - 0x107FF: Heap control block (unchanged)
- 0x10800 - 0x10FFF: Mesh + reserved (unchanged)
- 0x11000 - 0xFDFFF: Heap data (reduced by 8KB)
- 0xFE000 - 0xFEFFF: OBS ring (4KB)
- 0xFF000 - 0xFFFFF: ACTION ring (4KB)

If heap capacity cannot be reduced, increase ivshmem size to 2MB and
place the rings in the new space instead.

## Ring Header (shared for both rings)
```c
typedef struct {
  uint32_t magic;       /* RING_MAGIC */
  uint32_t head;        /* Producer index */
  uint32_t tail;        /* Consumer index */
  uint32_t size;        /* Entry count (power of 2) */
  uint32_t reserved[4]; /* Padding */
} ring_header_t;
```

## OBS Entry (Host -> Kernel)
Fixed-size record (32 bytes) so the ring can be memory-mapped and simple.
```c
typedef struct {
  uint32_t seq;         /* Monotonic step id */
  float    obs[4];      /* CartPole observation */
  float    reward;
  float    done;
  float    model_id;    /* For weight lookup (float32 id) */
} obs_entry_t;          /* 32 bytes */
```

## ACTION Entry (Kernel -> Host)
```c
typedef struct {
  uint32_t seq;         /* Matches obs seq */
  uint16_t action;      /* Discrete action */
  uint16_t flags;       /* Reserved for future */
  uint32_t ack_seq;     /* Optional: last obs seq consumed */
  uint32_t reserved;    /* Pad to 16 bytes */
} action_entry_t;       /* 16 bytes */
```

## Doorbell Policy
- Only ring the doorbell on empty -> non-empty transitions.
- Optionally batch doorbells every N entries for high-rate workloads.

## Control Flow
1) Kernel sends `CMD_ENV_RESET` with `ENV_RESET_FLAG_STREAM` to request streaming.
2) Host resets env, writes first OBS entry to obs ring, responds with `result=0`.
3) Kernel reads obs, writes ACTION entry, Host writes next OBS entry.

## Backpressure and Safety
- If action ring is full, kernel stalls or sleeps (short backoff).
- If obs ring is empty, kernel waits (poll + sleep).
- Rings are single-producer/single-consumer, no locks required.

## Compatibility
- Keep CMD_ENV_RESET for env setup, model upload, and metadata.
- CMD_ENV_STEP remains as fallback when rings are disabled.
