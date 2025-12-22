#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/keyboard.h"
#include "arch/pci.h"
#include "arch/pic.h"
#include "arch/pit.h"
#include "console.h"
#include "drivers/ivshmem.h"
#include "include/engine/episode.h"
#include "ipc/ipc.h"
#include "ipc/ipc_proto.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

/* Minimal serial output for debugging */
static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_char(char c) { outb(0x3F8, c); }

/* Main Kernel Entry Point */
void kmain64(void *multiboot_structure, uint32_t magic) {
  (void)multiboot_structure;
  (void)magic;

  console_cls();
  console_write("=== ZENEDGE KERNEL (x86_64) ===\n");
  console_write("Initializing Stage 1 (Arch)...\n");

  /* Core Arch setup */
  gdt_init();
  idt_init();
  pic_init();
  pit_init(100);
  keyboard_init();

  /* Memory Management */
  console_write("Initializing Memory Manager...\n");
  pmm_init(NULL); /* Pass NULL to trigger fallback */
  vmm_init();

  /* Hardware Integration */
  console_write("Scanning PCI Bus...\n");
  pci_init();

  console_write("Initializing IVSHMEM...\n");
  ivshmem_init();

  /* IPC Setup */
  uint64_t shmem_base = (uint64_t)ivshmem_get_shared_memory();
  uint8_t irq = ivshmem_get_irq();

  if (shmem_base) {
    console_write("IPC Shared Memory found at ");
    print_hex64(shmem_base);
    console_write("\n");
    ipc_init((void *)shmem_base, irq);

    /* Mesh & Engine */
    ipc_mesh_init();
    episode_init();

    /* Propose Initial Tuning Episode (Test) */
    episode_propose(1200, 500);
  } else {
    console_write("WARNING: No Shared Memory (Sidecar) found.\n");
  }

  /* Enable Interrupts */
  __asm__ __volatile__("sti");

  console_write("[kern] System Ready. Entering Main Loop.\n");

  /* Main Loop */
  while (1) {
    /* Drive the Safe Tuning Engine */
    episode_tick();

    /* Low-power wait */
    __asm__ __volatile__("hlt");
  }
}
