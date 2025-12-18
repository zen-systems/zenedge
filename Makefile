ARCH ?= i386

CC = clang
LD = ld
AS = clang
GRUB_MKRESCUE ?= grub-mkrescue
QEMU_UEFI ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_VARS_COPY := build/ovmf/OVMF_VARS.fd

# Architecture-specific flags
ifeq ($(ARCH),i386)
  # Cross-compile flags for i386 bare-metal
  # -mno-sse disables SSE which requires CPU initialization we haven't done
  # -mno-mmx disables MMX as well for safety
  TARGET  = -target i386-unknown-none-elf
  CFLAGS  = $(TARGET) -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
            -fno-builtin -fno-stack-protector -mno-red-zone -m32
  ASFLAGS = $(TARGET) -m32
  LDFLAGS = -T linker.ld -nostdlib -m elf_i386
  GRUB_MULTIBOOT_CMD = multiboot
  QEMU ?= qemu-system-i386
else ifeq ($(ARCH),x86_64)
  TARGET  = -target x86_64-unknown-none-elf
  CFLAGS  = $(TARGET) -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
            -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -m64 \
            -mcmodel=kernel
  ASFLAGS = $(TARGET) -m64
  LDFLAGS = -T arch/x86_64/linker.ld -nostdlib -m elf_x86_64
  GRUB_MULTIBOOT_CMD = multiboot2
  QEMU ?= qemu-system-x86_64
else
  $(error Unsupported ARCH=$(ARCH) (use i386 or x86_64))
endif

# Kernel sources
ifeq ($(ARCH),i386)
  SOURCES = kernel/kmain.c \
      kernel/console.c \
      kernel/contracts.c \
      kernel/zenedge_alloc.c \
      kernel/time/time.c \
      kernel/trace/flightrec.c \
      kernel/job/job_graph.c \
      kernel/sched/sched_core.c \
      kernel/sched/process.c \
      kernel/mm/pmm.c \
      kernel/mm/vmm.c \
      kernel/arch/gdt.c \
      kernel/arch/idt.c \
      kernel/arch/pic.c \
      kernel/arch/pit.c \
      kernel/arch/syscall.c \
      kernel/arch/keyboard.c \
      kernel/arch/pci.c \
      kernel/drivers/ivshmem.c \
      kernel/shell.c \
      kernel/ipc/ipc.c \
      kernel/ipc/heap.c \
      kernel/lib/divdi3.c \
      kernel/lib/math.c \
      kernel/lib/libc.c \
      kernel/lib/string.c \
      kernel/mm/kheap.c \
      kernel/wasm_loader.c \
      kernel/lib/wasm3/m3_core.c \
      kernel/lib/wasm3/m3_env.c \
      kernel/lib/wasm3/m3_code.c \
      kernel/lib/wasm3/m3_compile.c \
      kernel/lib/wasm3/m3_exec.c \
      kernel/lib/wasm3/m3_function.c \
      kernel/lib/wasm3/m3_info.c \
      kernel/lib/wasm3/m3_module.c \
      kernel/lib/wasm3/m3_parse.c \
      kernel/lib/wasm3/m3_bind.c
else ifeq ($(ARCH),x86_64)
  # Milestone M0: minimal long-mode bring-up (keeps i386 intact)
  SOURCES = kernel/kmain64.c
endif

# WASM3 Flags
# Disabled Float for first pass to avoid libm deps
WASM_FLAGS = -Dd_m3HasFloat=0 -Dd_m3FixedHeap=0 -Dd_m3Use32Bit=1 \
             -Dd_m3LogOutput=0 -Dd_m3VerboseErrorMessages=1 \
             -Dmalloc=kmalloc -Dfree=kfree -Drealloc=krealloc \
             -nostdinc -Ikernel/include \
             -I/usr/lib/llvm-18/lib/clang/18/include

# Include WASM_FLAGS in CFLAGS (i386 kernel currently builds wasm3 in-tree)
ifeq ($(ARCH),i386)
  CFLAGS += $(WASM_FLAGS)
endif

ifeq ($(ARCH),i386)
  SRC_S = \
      boot/multiboot_header.s \
      boot/start.s \
      kernel/arch/isr.s \
      kernel/arch/switch.s
else ifeq ($(ARCH),x86_64)
  SRC_S = \
      arch/x86_64/boot/multiboot2_header.s \
      arch/x86_64/boot/start.s
endif

all: zenedge.iso
iso: zenedge.iso

OBJDIR := build/$(ARCH)
OBJ := $(addprefix $(OBJDIR)/,$(SOURCES:.c=.o) $(SRC_S:.s=.o))

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

zenedge.bin: $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

grub-dir:
	mkdir -p iso/boot/grub

iso/boot/zenedge.bin: zenedge.bin grub-dir
	cp zenedge.bin iso/boot/zenedge.bin

iso/boot/grub/grub.cfg: grub-dir
	echo 'set timeout=0'                 >  iso/boot/grub/grub.cfg
	echo 'set default=0'                 >> iso/boot/grub/grub.cfg
	echo 'menuentry "ZENEDGE Kernel" {'  >> iso/boot/grub/grub.cfg
	echo '  $(GRUB_MULTIBOOT_CMD) /boot/zenedge.bin' >> iso/boot/grub/grub.cfg
	echo '  boot'                        >> iso/boot/grub/grub.cfg
	echo '}'                             >> iso/boot/grub/grub.cfg

zenedge.iso: iso/boot/zenedge.bin iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o zenedge.iso iso

run: zenedge.iso
	$(QEMU) -cdrom zenedge.iso

run-iso: run

run-serial: zenedge.iso
	$(QEMU) -cdrom zenedge.iso -serial stdio -display none

$(OVMF_VARS_COPY): $(OVMF_VARS)
	@mkdir -p $(dir $@)
	cp $(OVMF_VARS) $@

run-uefi: zenedge.iso $(OVMF_VARS_COPY)
	$(QEMU_UEFI) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS_COPY) \
		-cdrom zenedge.iso

run-serial-uefi: zenedge.iso $(OVMF_VARS_COPY)
	$(QEMU_UEFI) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS_COPY) \
		-cdrom zenedge.iso \
		-nographic

# Direct kernel boot (no ISO required)
run-direct: zenedge.bin
	$(QEMU) -kernel zenedge.bin -serial stdio

run-direct-vga: zenedge.bin
	$(QEMU) -kernel zenedge.bin

clean:
	rm -rf build zenedge.bin zenedge.iso iso

.PHONY: all iso run run-iso run-serial run-uefi run-serial-uefi run-direct run-direct-vga clean grub-dir
