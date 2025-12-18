# agents.md — Codex Agent Playbook (x86_64 Port)

This file is instructions for **Codex** (and human reviewers) to deploy multiple coding agents to upgrade ZenEdge from **i386** to **x86_64**.

## North Star

Deliver a bootable **x86_64** kernel that preserves the current design goals:

- Deterministic, “small kernel” philosophy
- Contracts + Flight Recorder remain first-class
- WASM agents remain supported (initially kernel-hosted is OK; user-mode later)
- Clean, testable milestones with QEMU automation

---

## Key Constraints

- **Do not break i386** until the x86_64 kernel boots (keep parallel arch directories).
- No “big rewrites” unless required by pointer-width. Prefer incremental migrations.
- Prefer **fixed-width types** and `uintptr_t` for pointer math (no `(uint32_t)ptr` casts).
- All new x86_64 code must compile with `-ffreestanding -fno-stack-protector -fno-pic -mno-red-zone`.
- Early boot must avoid `kmalloc()` until paging + heap init is known-good.

---

## Recommended Boot Strategy (Pick One and Commit)

### Preferred: Use a modern bootloader (Limine)
Using **Limine** dramatically reduces long-mode boot complexity and provides a clean handoff (memory map, framebuffer, SMP info, etc.).

**Deliverables**
- `boot/limine.cfg`
- `boot/limine/` (vendor files or submodule)
- ISO build target: `make iso && make run`

**Why**
- Minimizes brittle hand-written long-mode transition code
- Makes x86_64 bring-up faster and safer

### Alternative: Custom long-mode entry (only if required)
If you must own boot fully:
- 16-bit → 32-bit protected mode → 64-bit long mode
- Setup PML4/PDPT/PD/PT
- Enable PAE, EFER.LME, CR0.PG
- Jump to 64-bit `start64`

This is higher risk and should only be chosen if bootloader use is unacceptable.

---

## High-Level Architecture Target (x86_64)

### Virtual Memory Layout (proposal)
Choose a stable layout and use it everywhere:

- **Kernel higher-half base**: `KERN_BASE = 0xFFFFFFFF80000000`
- **Physical direct map (physmap)**: `PHYSMAP_BASE = 0xFFFF800000000000`
- **User space**: `0x0000000000400000` .. `0x00007FFFFFFFFFFF`
- Leave guard gaps between regions

### Paging
- Use 4-level paging (PML4 → PDPT → PD → PT)
- Use **2MiB huge pages** initially for kernel mapping (fast bring-up), then add 4KiB as needed.
- Keep kernel mapping identical in every address space.

### Interrupts
- Prefer **APIC** (Local APIC + IOAPIC) in x86_64 mode.
- PIC can be temporarily supported, but APIC is the target.
- Timer: PIT → APIC timer (later HPET if needed)

### Syscalls
- Use `SYSCALL/SYSRET` for x86_64 user↔kernel transitions.
- Define a stable syscall ABI early.

---

## Build + Run (Acceptance Commands)

Agents must keep these commands working (or update them and this file):

```bash
# build
make clean && make -j

# boot in qemu (serial on stdio)
make run

# optional: ISO boot
make iso && make run-iso
```

**QEMU baseline (example)**
- `qemu-system-x86_64 -serial stdio -m 1024 -no-reboot -d int -D qemu.log ...`

---

## Milestones and Definition of Done

### M0 — Toolchain + “Hello from Long Mode”
**Done when**
- Produces an ELF64 kernel and boots to a banner like:
  - `[boot] long mode ok`
  - `[kheap] initialized ...`
- Serial output works in QEMU.

### M1 — Paging + Higher-Half + Physmap
**Done when**
- Kernel runs at `KERN_BASE` in long mode.
- `phys_to_virt()` and `virt_to_phys()` are correct under the new layout.
- Frame allocator works with the bootloader memory map.

### M2 — IDT + Exceptions + Timer Tick
**Done when**
- Divide-by-zero and page fault handlers print valid info (RIP, error code, CR2).
- Timer interrupt increments a tick counter and preemption hook exists.

### M3 — Process Model MVP (x86_64)
**Done when**
- `proc_t` exists, scheduler runs, context switch works.
- User-mode transition works (ring3) with minimal user stub.
- User faults kill the proc without crashing kernel.

### M4 — WASM Agent Runtime Restored on x86_64
**Done when**
- wasm3 builds and runs the test module:
  - `[wasm] log_int: 42`
  - `[wasm] Execution Complete.`
- Memory limit hooks exist (ties into contracts later).

### M5 — APIC + SMP (Optional but Recommended)
**Done when**
- Local APIC enabled, IOAPIC routing works.
- Secondary CPUs boot (basic), print CPU id.

---

## Agent Deployment Plan (Parallelizable Work)

Codex can run these as separate “agents” / PRs. Each agent must keep scope tight and land behind compile-time arch gates.

### Agent A — Toolchain & Build System
**Goal**
- Add `arch/x86_64/` build support while keeping i386 intact.

**Tasks**
- Create `arch/x86_64/` directory tree (`boot/`, `mm/`, `cpu/`, `linker.ld`, etc.)
- Update Makefile for:
  - `ARCH=i386` (default) and `ARCH=x86_64`
  - correct flags (`-m64`, `-mcmodel=kernel`, `-mno-red-zone`)
- Update linker script for ELF64, higher-half base.
- Add `print_hex64()` and update console print helpers where needed.

**Acceptance**
- `make ARCH=x86_64 run` boots banner in QEMU.

---

### Agent B — Boot & Long Mode Entry
**Goal**
- Establish a clean x86_64 entrypoint and pass boot info to C.

**Tasks (Limine path)**
- Add Limine loader + config
- Implement `kmain64(boot_info*)`
- Verify stack setup, zero BSS, early serial

**Tasks (custom boot path)**
- Implement long-mode transition, minimal GDT, paging, jump to `start64`

**Acceptance**
- `[boot] long mode ok` printed reliably.

---

### Agent C — Memory Manager (x86_64 Paging + Physmap)
**Goal**
- Implement PML4 paging and a frame allocator using boot memory map.

**Tasks**
- Page table structs for 64-bit entries
- Higher-half kernel mapping + physmap mapping
- `frame_alloc() / frame_free()` (bitmap or freelist)
- `vmm_map_page()/unmap` stubs
- Ensure no stale i386 assumptions (`PSE 4MB` etc.)

**Acceptance**
- Can allocate frames, map pages, and access memory through physmap.

---

### Agent D — CPU/Interrupts (IDT, Exceptions, Timer)
**Goal**
- Reliable exception handling and periodic timer IRQ.

**Tasks**
- x86_64 IDT gate setup
- ISR stubs in assembly saving full register state
- `trapframe64_t` definition
- Page fault decoding (CR2, error flags)
- Timer interrupt path (PIT initially acceptable; target APIC timer)

**Acceptance**
- Forced page fault logs RIP/CR2 and returns/halts safely.
- Timer increments ticks.

---

### Agent E — Syscall ABI + User Transition
**Goal**
- Establish ring3 execution and syscall path.

**Tasks**
- GDT + TSS (IST optional but recommended for #PF/#DF)
- Setup MSRs for `SYSCALL/SYSRET`:
  - `IA32_LSTAR`, `IA32_STAR`, `IA32_FMASK`
- Define syscall ABI:
  - `rax=sysno, rdi,rsi,rdx,r10,r8,r9=args`
  - return in `rax`
- Minimal user stub program to test `sys_log()`.

**Acceptance**
- User stub executes, performs syscall, returns, and can be killed on fault.

---

### Agent F — WASM Runtime Bring-up on x86_64
**Goal**
- Re-enable wasm3 with hardened glue and validate.

**Tasks**
- Ensure wasm3 compiles with freestanding constraints
- Replace any 32-bit casts / assumptions
- Enforce memory limit via runtime config (tie to contract later)
- Add import set: `env.log_int`, `env.print`, `env.abort` (as needed)

**Acceptance**
- Test module prints expected output on x86_64.

---

## Coding Conventions for All Agents

- Prefer `size_t`, `uintptr_t`, `uint64_t` for addresses.
- Never print pointers with 32-bit helpers.
- Keep arch-specific code under:
  - `arch/i386/`
  - `arch/x86_64/`
- Shared code goes to `kernel/` or `lib/` only if truly arch-agnostic.

---

## Review Checklist (PR Gate)

Every PR must include:
- What milestone it advances (M0..M5)
- Boot log evidence (serial output)
- QEMU command used
- New files + touched files list
- Safety notes (panic paths, bounds checks, ABI changes)

---

## Notes on ONNX Runtime

ONNX Runtime upstream generally targets 64-bit and modern toolchains; once x86_64 is stable, prefer running ORT in a Linux sidecar/guest (shared memory IPC) rather than in-kernel. The kernel should enforce budgets; heavy ML belongs outside the TCB.
