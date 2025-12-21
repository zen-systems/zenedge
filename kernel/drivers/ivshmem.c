#include "ivshmem.h"
#include "../arch/pci.h"
#include "../arch/apic.h"
#include "../console.h"
#include "../mm/pmm.h" /* for PAGE_SIZE */
#include "../mm/vmm.h"

static uint32_t ivshmem_phys_base = 0;
static uint32_t ivshmem_size = 0;
static void *ivshmem_virt_base = 0;
static uint8_t ivshmem_irq = 0;
static int ivshmem_use_msi = 0;  /* Track if MSI is being used */

/* BAR0 (MMR) for doorbell support */
static volatile uint32_t *ivshmem_mmr_base = 0;
static uint32_t ivshmem_mmr_size = 0;

/* IVSHMEM MMR Register Offsets (BAR0) */
#define IVSHMEM_REG_INTRMASK    0x00  /* Interrupt Mask */
#define IVSHMEM_REG_INTRSTATUS  0x04  /* Interrupt Status */
#define IVSHMEM_REG_IVPOSITION  0x08  /* IV Position (peer ID) */
#define IVSHMEM_REG_DOORBELL    0x0C  /* Doorbell */

#include "../arch/pic.h"

static ivshmem_irq_callback_t ivshmem_callback = 0;

/* IRQ Handler wrapper */
static void ivshmem_isr_handler(interrupt_frame_t *frame) {
    (void)frame;

    /* Clear interrupt status if MMR is mapped */
    if (ivshmem_mmr_base) {
        /* Read and clear interrupt status */
        volatile uint32_t status = ivshmem_mmr_base[IVSHMEM_REG_INTRSTATUS / 4];
        (void)status;  /* Acknowledge by reading */
    }

    if (ivshmem_callback) {
        ivshmem_callback();
    }

    /* Send EOI to LAPIC for MSI interrupts */
    if (ivshmem_use_msi) {
        lapic_eoi();
    }
}

void ivshmem_set_callback(ivshmem_irq_callback_t cb) {
    ivshmem_callback = cb;
}

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

    /* Map BAR0 */
    uint32_t bar0_size = 0;
    uint32_t bar0 = pci_get_bar(dev, 0, &bar0_size);
    
    console_write("[ivshmem] BAR0 Size: ");
    print_uint(bar0_size);
    console_write("\n");

    /* Heuristic: If BAR0 is large (>= 4KB), it's Shared Memory (ivshmem-plain).
       If small (<= 1KB, usually 256B), it's MMR (ivshmem-doorbell). */
    
    if (bar0_size >= 4096) {
        console_write("[ivshmem] Detected ivshmem-plain (BAR0 is Shared Memory)\n");
        ivshmem_mmr_base = NULL;
        ivshmem_mmr_size = 0;
        
        ivshmem_phys_base = bar0;
        ivshmem_size = bar0_size;
        
        /* Map Memory */
        #define IVSHMEM_VIRT_START 0xE0000000
        /* Uncached for safety, though Shared RAM is usually Coherent */
        if (vmm_map_range(IVSHMEM_VIRT_START, ivshmem_phys_base, ivshmem_size, 0x13) != 0) {
             console_write("[ivshmem] Failed to map memory!\n");
             return;
        }
        ivshmem_virt_base = (void*)IVSHMEM_VIRT_START;
        console_write("[ivshmem] Mapped to Virt: ");
        print_hex32((uint32_t)ivshmem_virt_base);
        console_write("\n");
        
        /* Test Memory */
        volatile uint32_t *ptr = (volatile uint32_t*)ivshmem_virt_base;
        ptr[0] = 0xCAFEBABE;
        if (ptr[0] == 0xCAFEBABE) {
            console_write("[ivshmem] Memory Test Passed\n");
            ptr[0] = 0;
        } else {
            console_write("[ivshmem] Memory Test Failed! Read: ");
            print_hex32(ptr[0]);
            console_write("\n");
        }
        
    } else {
        /* Standard ivshmem-doorbell logic with MMR at BAR0, Mem at BAR2 */
        if (bar0 != 0 && bar0_size > 0) {
            #define IVSHMEM_MMR_VIRT 0xE1000000
            if (vmm_map_range(IVSHMEM_MMR_VIRT, bar0, bar0_size,
                              PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE) == 0) {
                ivshmem_mmr_base = (volatile uint32_t *)IVSHMEM_MMR_VIRT;
                ivshmem_mmr_size = bar0_size;
                console_write("[ivshmem] MMR (BAR0) mapped at ");
                print_hex32(bar0);
                console_write(" -> ");
                print_hex32(IVSHMEM_MMR_VIRT);
                console_write("\n");
                ivshmem_mmr_base[IVSHMEM_REG_INTRMASK / 4] = 0; 
            }
        }
        
        /* Try MSI / IRQ setup (Existing code...) */
        // ... (Interrupt setup code here, I need to keep it or just skip/simplify?) 
        /* The existing code is linear. I can't just 'else' big block easily with replace.
           I should restructure: first detect type, then setup.
           Simplest:
           if (bar0_size >= 4096) { setup plain; return; }
           
           // proceed with doorbell setup
        */
    }
    
    // Actually, I can just return after plain setup.
    // The previous code had `bar2` logic at the end.
    // I need to be careful not to execute `bar2` logic if plain.
    // So `return` is good.
    
    if (bar0_size >= 4096) return; // Done for plain
    
    // ... Continue with doorbell logic (BAR0 map, MSI, BAR2 map)
    // Re-insert the original MMR mapping code here?
    // My previous replacement replaced init start.
    // I'll rewrite init fully to be clean.

    /* Try MSI first */
    /* Use vector 50 (0x32) + basic prioritization */
    /* Get actual APIC ID instead of hardcoding 0 */
    uint8_t apic_id = (uint8_t)lapic_get_id();
    if (pci_enable_msi(dev, 50, apic_id) == 0) {
        /* Register handler for Vector 50 */
        console_write("[ivshmem] Registering MSI handler on vector 50\n");
        idt_register_handler(50, ivshmem_isr_handler);
        ivshmem_irq = 50; /* For info */
        ivshmem_use_msi = 1;  /* Mark that we're using MSI */
    } else {
        ivshmem_irq = 0; /* Fallback to legacy */
        ivshmem_use_msi = 0;
    }

    /* Legacy IRQ Fallback (only if we didn't get MSI? actually pci_enable_msi prints if check fails)
       For now, we just do both or assume MSI priority. 
       If MSI worked, the device wont assert legacy INTx line usually.
    */
    if (ivshmem_irq == 0) {
        /* Read Interrupt Line (Offset 0x3C) */
        uint32_t irq_reg = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x3C);
        ivshmem_irq = irq_reg & 0xFF; /* Lower 8 bits is Interrupt Line */

        console_write("[ivshmem] Legacy IRQ: ");
        print_uint(ivshmem_irq);
        console_write("\n");
        
        if (ivshmem_irq > 0 && ivshmem_irq < 16 && ivshmem_irq != 0xFF) {
          console_write("[ivshmem] registering Legacy IRQ handler on vector ");
          print_uint(32 + ivshmem_irq);
          console_write("\n");

          idt_register_handler(32 + ivshmem_irq, ivshmem_isr_handler);
          pic_unmask_irq(ivshmem_irq);
        } else {
          console_write("[ivshmem] Warning: Invalid IRQ, polling mode only.\n");
        }
    }

    /* BAR2 contains the shared memory region for ivshmem-plain devices. */
    uint32_t size_mask = 0;
    uint32_t bar2 = pci_get_bar(dev, 2, &size_mask);

    if (bar2 == 0) {
        console_write("[ivshmem] Warning: BAR2 Unassigned. Fixing...\n");
        bar2 = 0xA0000000; 
        
        /* Disable Decoding first? */
        /* Write Lower */
        pci_write_config_32(dev.bus, dev.slot, dev.func, 0x18, bar2);
        /* Write Upper (0) */
        pci_write_config_32(dev.bus, dev.slot, dev.func, 0x1C, 0);
        
        /* Read Back */
        /* uint32_t bar2_check = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x18); */
        
        if ((pci_read_config_32(dev.bus, dev.slot, dev.func, 0x18) & 0xFFFFFFF0) != bar2) {
             console_write("[ivshmem] ERROR: BAR2 assignment failed!\n");
        }
        
        /* Enable Memory Space (Bit 1) and Bus Master (Bit 2) */
        uint32_t cmd = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x04);
        cmd |= 0x06; 
        pci_write_config_32(dev.bus, dev.slot, dev.func, 0x04, cmd);
        
        console_write("[ivshmem] Assigned BAR2 to 0xA0000000 and enabled decoding\n");
    }

    ivshmem_phys_base = bar2;
    ivshmem_size = size_mask;

    /* Map to Virtual Memory (Use Identity Mapping because VMM is stub) */
    ivshmem_virt_base = (void*)(uintptr_t)ivshmem_phys_base;
    
    if (vmm_map_range((uintptr_t)ivshmem_virt_base, ivshmem_phys_base, ivshmem_size,
                      0x03) != 0) {
      console_write("[ivshmem] Failed to map memory!\n");
      return;
    }

    console_write("[ivshmem] Mapped to Virt: ");
    print_hex32((uint32_t)ivshmem_virt_base);
    console_write(" (Identity)\n");

  } else {
    console_write("[ivshmem] Device not found.\n");
  }
}

void *ivshmem_get_shared_memory(void) { return ivshmem_virt_base; }

uint32_t ivshmem_get_size(void) { return ivshmem_size; }

uint8_t ivshmem_get_irq(void) { return ivshmem_irq; }

/* Get our peer ID (IV Position) */
uint32_t ivshmem_get_peer_id(void) {
    if (!ivshmem_mmr_base) return 0;
    return ivshmem_mmr_base[IVSHMEM_REG_IVPOSITION / 4];
}

/* Ring the doorbell to notify a peer
 * peer_id: target peer's IV position
 * vector: interrupt vector to trigger on peer (typically 0)
 */
void ivshmem_ring_doorbell(uint32_t peer_id, uint32_t vector) {
    if (!ivshmem_mmr_base) {
        console_write("[ivshmem] Warning: Doorbell not available (BAR0 not mapped)\n");
        return;
    }
    /* Doorbell register format: (peer_id << 16) | vector */
    uint32_t doorbell_val = (peer_id << 16) | (vector & 0xFFFF);
    ivshmem_mmr_base[IVSHMEM_REG_DOORBELL / 4] = doorbell_val;
}

/* Check if doorbell/interrupt support is available */
int ivshmem_has_doorbell(void) {
    return ivshmem_mmr_base != 0;
}
