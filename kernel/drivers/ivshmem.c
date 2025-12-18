#include "ivshmem.h"
#include "../arch/pci.h"
#include "../console.h"
#include "../mm/pmm.h" /* for PAGE_SIZE */
#include "../mm/vmm.h"

static uint32_t ivshmem_phys_base = 0;
static uint32_t ivshmem_size = 0;
static void *ivshmem_virt_base = 0;
static uint8_t ivshmem_irq = 0;

void ivshmem_init(void) {
  pci_device_t dev;
  if (pci_find_device(IVSHMEM_VENDOR_ID, IVSHMEM_DEVICE_ID, &dev)) {
    console_write("[ivshmem] Found device at ");
    print_uint(dev.bus);
    console_write(":");
    print_uint(dev.slot);
    console_write(".");
    print_uint(dev.func);
    console_write("\n");

    /* Read Interrupt Line (Offset 0x3C) */
    uint32_t irq_reg = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x3C);
    ivshmem_irq = irq_reg & 0xFF; /* Lower 8 bits is Interrupt Line */

    console_write("[ivshmem] IRQ: ");
    print_uint(ivshmem_irq);
    console_write("\n");

    /* BAR0 contains the MMIO region for the edu device.
       We will treat this as our "Shared Memory" for IPC/Sidecar testing. */

    uint32_t size_mask = 0;
    uint32_t bar0 = pci_get_bar(dev, 0, &size_mask);

    ivshmem_phys_base = bar0;
    ivshmem_size = size_mask;

    console_write("[ivshmem] Shared Memory Phys: ");
    print_hex32(ivshmem_phys_base);
    console_write(" Size: ");
    print_uint(ivshmem_size);
    console_write(" bytes\n");

    /* Map to Virtual Memory */
    /* For simplicity, we map it 1:1 or to a specific high memory region?
       Let's let vmm map it to a high address or just iterate.
       Wait, vmm_map_range handles mapping physical to virtual.
       We need to pick a virtual address. Let's use 0xE0000000 (3.5GB mark) */

#define IVSHMEM_VIRT_START 0xE0000000

    /* Ensure we map enough pages */
    /* PTE_PRESENT | PTE_RW (0x03) */
    if (vmm_map_range(IVSHMEM_VIRT_START, ivshmem_phys_base, ivshmem_size,
                      0x03) != 0) {
      console_write("[ivshmem] Failed to map memory!\n");
      return;
    }

    ivshmem_virt_base = (void *)IVSHMEM_VIRT_START;
    console_write("[ivshmem] Mapped to Virt: ");
    print_hex32((uint32_t)ivshmem_virt_base);
    console_write("\n");

  } else {
    console_write("[ivshmem] Device not found.\n");
  }
}

void *ivshmem_get_shared_memory(void) { return ivshmem_virt_base; }

uint8_t ivshmem_get_irq(void) { return ivshmem_irq; }
