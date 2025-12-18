# Makefile
#
# Cross-compilation for bare-metal i386
# Uses clang on macOS with ld.lld for ELF output

CC      = clang
LD      = ld
AS      = clang

# Cross-compile flags for i386 bare-metal
# -mno-sse disables SSE which requires CPU initialization we haven't done
# -mno-mmx disables MMX as well for safety
TARGET  = -target i386-unknown-none-elf
CFLAGS  = $(TARGET) -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
          -fno-builtin -fno-stack-protector -mno-red-zone -m32 \
          -fno-builtin -fno-stack-protector -mno-red-zone -m32
ASFLAGS = $(TARGET) -m32
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

# Kernel sources
SOURCES = kernel/kmain.c \
    kernel/console.c \
    kernel/contracts.c \
    kernel/zenedge_alloc.c \
    kernel/time/time.c \
    kernel/trace/flightrec.c \
    kernel/job/job_graph.c \
    kernel/sched/sched_core.c \
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
    kernel/lib/math.c

SRC_S = \
    boot/multiboot_header.s \
    boot/start.s \
    kernel/arch/isr.s \
    kernel/arch/switch.s

OBJ    = $(SOURCES:.c=.o) $(SRC_S:.s=.o)

all: zenedge.iso

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) -c $< -o $@

zenedge.bin: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

grub-dir:
	mkdir -p iso/boot/grub

iso/boot/zenedge.bin: zenedge.bin grub-dir
	cp zenedge.bin iso/boot/zenedge.bin

iso/boot/grub/grub.cfg: grub-dir
	echo 'set timeout=0'                 >  iso/boot/grub/grub.cfg
	echo 'set default=0'                 >> iso/boot/grub/grub.cfg
	echo 'menuentry "ZENEDGE Kernel" {'  >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/zenedge.bin' >> iso/boot/grub/grub.cfg
	echo '  boot'                        >> iso/boot/grub/grub.cfg
	echo '}'                             >> iso/boot/grub/grub.cfg

zenedge.iso: iso/boot/zenedge.bin iso/boot/grub/grub.cfg
	i686-elf-grub-mkrescue -o zenedge.iso iso

run: zenedge.iso
	qemu-system-i386 -cdrom zenedge.iso

run-serial: zenedge.iso
	qemu-system-i386 -cdrom zenedge.iso -serial stdio -display none

# Direct kernel boot (no ISO required)
run-direct: zenedge.bin
	qemu-system-i386 -kernel zenedge.bin -serial stdio

run-direct-vga: zenedge.bin
	qemu-system-i386 -kernel zenedge.bin

clean:
	rm -rf $(OBJ) zenedge.bin zenedge.iso iso

.PHONY: all clean run run-serial run-direct run-direct-vga grub-dir
