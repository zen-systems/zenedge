#ifndef _ARCH_PCI_H
#define _ARCH_PCI_H

#include <stdint.h>

/* PCI I/O Ports */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* PCI Config Space Offsets */
#define PCI_REGISTER_VENDOR_ID 0x00
#define PCI_REGISTER_DEVICE_ID 0x02
#define PCI_REGISTER_COMMAND 0x04
#define PCI_REGISTER_STATUS 0x06
#define PCI_REGISTER_REVISION_ID 0x08
#define PCI_REGISTER_PROG_IF 0x09
#define PCI_REGISTER_SUBCLASS 0x0A
#define PCI_REGISTER_CLASS_CODE 0x0B
#define PCI_REGISTER_HEADER_TYPE 0x0E
#define PCI_REGISTER_BAR0 0x10
#define PCI_REGISTER_BAR1 0x14
#define PCI_REGISTER_BAR2 0x18
#define PCI_REGISTER_BAR3 0x1C
#define PCI_REGISTER_BAR4 0x20
#define PCI_REGISTER_BAR5 0x24
#define PCI_REGISTER_INTERRUPT_LINE 0x3C
#define PCI_REGISTER_INTERRUPT_PIN 0x3D

/* Command Register Bits */
#define PCI_COMMAND_IO 0x01
#define PCI_COMMAND_MEMORY 0x02
#define PCI_COMMAND_BUS_MASTER 0x04

/* PCI Device Structure */
typedef struct {
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
} pci_device_t;

/* Public API */
void pci_init(void);
uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset);
void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset, uint32_t val);

/* Find a device by Vendor/Device ID */
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    pci_device_t *dev_out);

/* Get BAR physical address and size */
/* type_out: 0=start_addr, 1=size */
uint32_t pci_get_bar(pci_device_t dev, int bar_index, uint32_t *size_out);

/* Enable MSI */
int pci_enable_msi(pci_device_t dev, uint8_t vector, uint8_t dest_id);

#endif /* _ARCH_PCI_H */
