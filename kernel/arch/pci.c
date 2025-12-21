#include "pci.h"
#include "../console.h"

/* Helper for I/O ports */
static inline void outl(uint16_t port, uint32_t val) {
  __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset) {
  uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
                                (offset & 0xFC) | ((uint32_t)0x80000000));
  outl(PCI_CONFIG_ADDRESS, address);
  return inl(PCI_CONFIG_DATA);
}

void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset, uint32_t val) {
  uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
                                (offset & 0xFC) | ((uint32_t)0x80000000));
  outl(PCI_CONFIG_ADDRESS, address);
  outl(PCI_CONFIG_DATA, val);
}

/* Check if device exists at bus/slot/func */
static void pci_check_device(uint8_t bus, uint8_t slot, uint8_t func) {
  uint32_t id_reg = pci_read_config_32(bus, slot, func, 0x00);
  uint16_t vendor = id_reg & 0xFFFF;

  if (vendor == 0xFFFF)
    return; /* Device doesn't exist */

  uint16_t device = (id_reg >> 16) & 0xFFFF;

  /* Read class code / subclass */
  uint32_t class_reg = pci_read_config_32(bus, slot, func, 0x08);
  uint8_t class_code = (class_reg >> 24) & 0xFF;
  uint8_t subclass = (class_reg >> 16) & 0xFF;

  console_write("[pci] Found ");
  print_hex32((uint32_t)vendor);
  console_write(":");
  print_hex32((uint32_t)device);
  console_write(" at ");
  print_uint(bus);
  console_write(":");
  print_uint(slot);
  console_write(".");
  print_uint(func);
  console_write(" (Class ");
  print_hex32(class_code);
  console_write(")\n");

  /* Identify specific known devices */
  if (vendor == 0x10DE) {
      console_write("[pci] ^-- NVIDIA GPU Detected");
      if (device == 0x2484) console_write(" (RTX 3070)");
      console_write("\n");
  }
}

void pci_init(void) {
  console_write("[pci] Scanning Bus 0...\n");

  /* Brute force scan Bus 0 */
  for (uint8_t slot = 0; slot < 32; slot++) {
    /* Check func 0 */
    pci_check_device(0, slot, 0);

    /* Check if multi-function? (Optimization skipped for now, just checking
       func 0-7 is safe but slower, or checking header type of func 0 to see if
       it's MF) For now, scanning all functions: */
    for (uint8_t func = 1; func < 8; func++) {
      uint32_t id_reg = pci_read_config_32(0, slot, func, 0x00);
      if ((id_reg & 0xFFFF) != 0xFFFF) {
        pci_check_device(0, slot, func);
      }
    }
  }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    pci_device_t *dev_out) {
  for (uint8_t slot = 0; slot < 32; slot++) {
    for (uint8_t func = 0; func < 8; func++) {
      uint32_t id_reg = pci_read_config_32(0, slot, func, 0x00);
      if ((id_reg & 0xFFFF) == vendor_id &&
          ((id_reg >> 16) & 0xFFFF) == device_id) {
        if (dev_out) {
          dev_out->bus = 0;
          dev_out->slot = slot;
          dev_out->func = func;
          dev_out->vendor_id = vendor_id;
          dev_out->device_id = device_id;
        }
        return 1; /* Found */
      }
    }
  }
  return 0; /* Not found */
}

uint32_t pci_get_bar(pci_device_t dev, int bar_index, uint32_t *size_out) {
  uint8_t offset = PCI_REGISTER_BAR0 + (bar_index * 4);

  /* 1. Read original value */
  uint32_t orig_val = pci_read_config_32(dev.bus, dev.slot, dev.func, offset);

  /* 2. Write all 1s to size it */
  pci_write_config_32(dev.bus, dev.slot, dev.func, offset, 0xFFFFFFFF);

  /* 3. Read back encoded size */
  uint32_t size_encoded =
      pci_read_config_32(dev.bus, dev.slot, dev.func, offset);

  /* 4. Restore original value */
  pci_write_config_32(dev.bus, dev.slot, dev.func, offset, orig_val);

  /* Mask info bits */
  uint32_t size_mask = ~(size_encoded & 0xFFFFFFF0) + 1;
  if (size_out)
    *size_out = size_mask;

  return orig_val & 0xFFFFFFF0; /* Return base address (Memory) */
}

/* MSI Capability ID = 0x05 */
#define PCI_CAP_ID_MSI 0x05

int pci_enable_msi(pci_device_t dev, uint8_t vector, uint8_t dest_id) {
    uint8_t status = pci_read_config_32(dev.bus, dev.slot, dev.func, PCI_REGISTER_STATUS) >> 16;
    
    if (!(status & 0x10)) {
        console_write("[pci] Capabilities List not supported\n");
        return -1;
    }
    
    uint8_t cap_ptr = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x34) & 0xFF;
    
    while (cap_ptr != 0) {
        uint32_t cap_hdr = pci_read_config_32(dev.bus, dev.slot, dev.func, cap_ptr);
        uint8_t cap_id = cap_hdr & 0xFF;
        uint8_t next_cap = (cap_hdr >> 8) & 0xFF;
        
        if (cap_id == PCI_CAP_ID_MSI) {
            console_write("[pci] Found MSI Capability at offset ");
            print_hex32(cap_ptr);
            console_write("\n");
            
            /* MSI Control Register at offset +2 */
            /* 32-bit read at cap_ptr includes ID (0), Next (8), Control (16) */
            uint16_t msi_ctl = (cap_hdr >> 16);
            
            /* 
             * Address: 0xFEE00000 | (Dest << 12) 
             * Data: Vector | Edge(0) | Fixed(0)
             */
            uint32_t addr = 0xFEE00000 | (dest_id << 12);
            uint32_t data = vector;
            
            /* Message Address at cap_ptr + 4 */
            pci_write_config_32(dev.bus, dev.slot, dev.func, cap_ptr + 4, addr);
            
            /* Message Upper Address at cap_ptr + 8 (if 64-bit supported) */
            /* Check bit 7 of control for 64-bit capable */
            int is_64bit = (msi_ctl & 0x0080);
            
            if (is_64bit) {
                pci_write_config_32(dev.bus, dev.slot, dev.func, cap_ptr + 8, 0);
                pci_write_config_32(dev.bus, dev.slot, dev.func, cap_ptr + 12, data);
            } else {
                pci_write_config_32(dev.bus, dev.slot, dev.func, cap_ptr + 8, data);
            }
            
            /* Enable MSI (Bit 0 of Control) */
            msi_ctl |= 0x1;
            
            /* Write back control (careful to preserve ID/Next bytes if writing 32-bit word) */
            cap_hdr = (cap_hdr & 0xFFFF) | ((uint32_t)msi_ctl << 16);
            pci_write_config_32(dev.bus, dev.slot, dev.func, cap_ptr, cap_hdr);
            
            console_write("[pci] MSI Enabled for vector ");
            print_uint(vector);
            console_write("\n");
            return 0;
        }
        
        cap_ptr = next_cap;
    }
    
    console_write("[pci] MSI Capability not found\n");
    return -1;
}
