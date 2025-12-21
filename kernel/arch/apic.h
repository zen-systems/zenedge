#ifndef _ARCH_APIC_H
#define _ARCH_APIC_H

#include <stdint.h>

/* MSRs */
#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800

/* Local APIC Registers (Offsets) */
#define LAPIC_ID            0x020
#define LAPIC_VER           0x030
#define LAPIC_TPR           0x080
#define LAPIC_EOI           0x0B0
#define LAPIC_SVR           0x0F0
#define LAPIC_ESR           0x280
#define LAPIC_LVT_CMCI      0x2F0
#define LAPIC_ICR_LOW       0x300 /* Interrupt Command Register (0-31) */
#define LAPIC_ICR_HIGH      0x310 /* Interrupt Command Register (32-63) */
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_LVT_THERMAL   0x330
#define LAPIC_LVT_PERF      0x340
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TICR          0x380 /* Initial Count */
#define LAPIC_TCCR          0x390 /* Current Count */
#define LAPIC_TDCR          0x3E0 /* Divide Config */

/* Spurious Interrupt Vector Register Bits */
#define APIC_SVR_ENABLE     0x100

void lapic_init(void);
void lapic_eoi(void);
void lapic_write(uint32_t reg, uint32_t value);
uint32_t lapic_read(uint32_t reg);
uint32_t lapic_get_id(void);

#endif /* _ARCH_APIC_H */
