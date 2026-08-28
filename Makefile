# GNU/Windows Makefile

BUILD_DIR   := build
KERNEL_DIR  := kernel
BOOT_DIR    := boot

CC          := clang
LD          := ld.lld

KERNEL_CFLAGS := -target x86_64-unknown-none-elf \
                 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
                 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
                 -Wall -Wextra -Werror -std=c11 -O2 -g \
                 -I$(KERNEL_DIR)/include

KERNEL_ASFLAGS := -target x86_64-unknown-none-elf -c
KERNEL_LDFLAGS := -nostdlib -static -T $(KERNEL_DIR)/arch/x86_64/linker/kernel.ld

BOOT_CFLAGS := -target x86_64-unknown-windows \
               -ffreestanding -fno-stack-protector -fshort-wchar \
               -mno-red-zone -Wall -Wextra -std=c11 -O2 -g \
               -I$(BOOT_DIR)/include

BOOT_LDFLAGS := -target x86_64-unknown-windows \
                -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application \
                -fuse-ld=lld

.PHONY: all clean kernel bootloader image run dirs userelf

all: image

dirs:
	mkdir -p $(BUILD_DIR)/kernel/arch/x86_64/boot
	mkdir -p $(BUILD_DIR)/kernel/arch/x86_64/cpu
	mkdir -p $(BUILD_DIR)/kernel/arch/x86_64/interrupts
	mkdir -p $(BUILD_DIR)/kernel/core $(BUILD_DIR)/kernel/mm
	mkdir -p $(BUILD_DIR)/kernel/sched $(BUILD_DIR)/kernel/vfs
	mkdir -p $(BUILD_DIR)/kernel/syscall $(BUILD_DIR)/kernel/process
	mkdir -p $(BUILD_DIR)/boot

# Userspace ELF
$(BUILD_DIR)/hello.elf: $(KERNEL_DIR)/process/userprog.S $(KERNEL_DIR)/process/user.ld | dirs
	$(CC) $(KERNEL_ASFLAGS) $(KERNEL_DIR)/process/userprog.S -o $(BUILD_DIR)/userprog_elf.o
	$(LD) -nostdlib -static -T $(KERNEL_DIR)/process/user.ld -o $@ $(BUILD_DIR)/userprog_elf.o
	@echo "Built $@"

$(BUILD_DIR)/hello_blob.o: $(BUILD_DIR)/hello.elf
	cd $(BUILD_DIR) && $(LD) -m elf_x86_64 -r -b binary -o hello_blob.o hello.elf

userelf: $(BUILD_DIR)/hello.elf $(BUILD_DIR)/hello_blob.o

$(BUILD_DIR)/kernel/arch/x86_64/boot/entry.o: $(KERNEL_DIR)/arch/x86_64/boot/entry.S | dirs
	$(CC) $(KERNEL_ASFLAGS) $< -o $@
$(BUILD_DIR)/kernel/arch/x86_64/interrupts/isr.o: $(KERNEL_DIR)/arch/x86_64/interrupts/isr.S | dirs
	$(CC) $(KERNEL_ASFLAGS) $< -o $@
$(BUILD_DIR)/kernel/arch/x86_64/cpu/switch.o: $(KERNEL_DIR)/arch/x86_64/cpu/switch.S | dirs
	$(CC) $(KERNEL_ASFLAGS) $< -o $@
$(BUILD_DIR)/kernel/process/userprog.o: $(KERNEL_DIR)/process/userprog.S | dirs
	$(CC) $(KERNEL_ASFLAGS) $< -o $@
$(BUILD_DIR)/kernel/process/process_asm.o: $(KERNEL_DIR)/process/process_asm.S | dirs
	$(CC) $(KERNEL_ASFLAGS) $< -o $@

$(BUILD_DIR)/kernel/core/%.o: $(KERNEL_DIR)/core/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/mm/%.o: $(KERNEL_DIR)/mm/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/sched/%.o: $(KERNEL_DIR)/sched/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/vfs/%.o: $(KERNEL_DIR)/vfs/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/syscall/%.o: $(KERNEL_DIR)/syscall/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/process/%.o: $(KERNEL_DIR)/process/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/pkg/%.o: $(KERNEL_DIR)/pkg/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/net/%.o: $(KERNEL_DIR)/net/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/drivers/%.o: $(KERNEL_DIR)/drivers/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/fs/%.o: $(KERNEL_DIR)/fs/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/arch/x86_64/cpu/%.o: $(KERNEL_DIR)/arch/x86_64/cpu/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/arch/x86_64/interrupts/%.o: $(KERNEL_DIR)/arch/x86_64/interrupts/%.c | dirs
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

KERNEL_OBJS := \
	$(BUILD_DIR)/kernel/arch/x86_64/boot/entry.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/switch.o \
	$(BUILD_DIR)/kernel/arch/x86_64/interrupts/isr.o \
	$(BUILD_DIR)/kernel/arch/x86_64/interrupts/handlers.o \
	$(BUILD_DIR)/kernel/arch/x86_64/interrupts/irq.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/gdt.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/idt.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/cpu.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/pic.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/pit.o \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu/timer.o \
	$(BUILD_DIR)/kernel/mm/pmm.o \
	$(BUILD_DIR)/kernel/mm/heap.o \
	$(BUILD_DIR)/kernel/mm/vmm.o \
	$(BUILD_DIR)/kernel/drivers/ata.o \
	$(BUILD_DIR)/kernel/drivers/ne2k.o \
	$(BUILD_DIR)/kernel/drivers/vga.o \
	$(BUILD_DIR)/kernel/drivers/kbd.o \
	$(BUILD_DIR)/kernel/drivers/rtc.o \
	$(BUILD_DIR)/kernel/drivers/pci.o \
	$(BUILD_DIR)/kernel/fs/fat.o \
	$(BUILD_DIR)/kernel/vfs/vfs.o \
	$(BUILD_DIR)/kernel/syscall/syscall.o \
	$(BUILD_DIR)/kernel/process/userprog.o \
	$(BUILD_DIR)/kernel/process/process_asm.o \
	$(BUILD_DIR)/kernel/process/process.o \
	$(BUILD_DIR)/kernel/process/elf.o \
	$(BUILD_DIR)/kernel/pkg/pkg.o \
	$(BUILD_DIR)/kernel/net/net.o \
	$(BUILD_DIR)/kernel/net/tcp.o \
	$(BUILD_DIR)/kernel/net/http.o \
	$(BUILD_DIR)/kernel/net/udp.o \
	$(BUILD_DIR)/kernel/net/dns.o \
	$(BUILD_DIR)/hello_blob.o \
	$(BUILD_DIR)/kernel/sched/thread.o \
	$(BUILD_DIR)/kernel/sched/sched.o \
	$(BUILD_DIR)/kernel/core/console.o \
	$(BUILD_DIR)/kernel/core/kmain.o \
	$(BUILD_DIR)/kernel/core/serial.o

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJS)
	$(LD) $(KERNEL_LDFLAGS) -o $@ $^
	@echo "Built $@"

kernel: userelf $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/boot/bootloader.o: $(BOOT_DIR)/bootloader.c | dirs
	$(CC) $(BOOT_CFLAGS) -c $< -o $@
$(BUILD_DIR)/BOOTX64.EFI: $(BUILD_DIR)/boot/bootloader.o
	$(CC) $(BOOT_LDFLAGS) -o $@ $<

bootloader: $(BUILD_DIR)/BOOTX64.EFI

$(BUILD_DIR)/gnuwindows.img: $(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/kernel.elf
	./scripts/create_image.sh $@ $(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/kernel.elf

image: $(BUILD_DIR)/gnuwindows.img
clean:
	rm -rf $(BUILD_DIR)
run: image
	./scripts/run_qemu.sh $(BUILD_DIR)/gnuwindows.img
