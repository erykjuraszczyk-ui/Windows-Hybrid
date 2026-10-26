SHELL := /bin/bash
.DEFAULT_GOAL := image

BUILD_DIR := build
LIMINE_VERSION := v12.9.4
LIMINE_RELEASE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/$(LIMINE_VERSION)/limine-binary.tar.gz
LIMINE_BINARY_SHA256 := 847334bc4bc142de996fbd132806b89ed73ac9ab34dbc1e90cfebf1fcc68c2b1
LIMINE_PROTOCOL_COMMIT := 3a0526b700e356f0eac1b71a77697b3fd1c707a3
LIMINE_DIR := $(BUILD_DIR)/limine-binary
PROTOCOL_DIR := $(BUILD_DIR)/limine-protocol
KERNEL_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/console.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/scheduler.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/interrupt_stubs.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO_ROOT := $(BUILD_DIR)/iso_root
ISO_IMAGE := $(BUILD_DIR)/hybrid.iso
LIMINE_ARCHIVE := $(BUILD_DIR)/limine-binary.tar.gz

CC := gcc
LD := ld
CFLAGS := -std=gnu17 -Wall -Wextra -Werror -O2 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -march=x86-64 -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse
ASMFLAGS := -g -m64 -mno-red-zone -fno-pic -fno-pie
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld

.PHONY: all deps kernel image run test clean
all: image

deps: $(LIMINE_DIR)/limine $(PROTOCOL_DIR)/.pinned

$(LIMINE_ARCHIVE):
	@mkdir -p $(BUILD_DIR)
	curl --fail --location --retry 3 --output $@.tmp "$(LIMINE_RELEASE_URL)"
	echo "$(LIMINE_BINARY_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(LIMINE_DIR)/limine: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	mkdir -p $(BUILD_DIR)
	tar --extract --gzip --file $< --directory $(BUILD_DIR)
	$(MAKE) -C $(LIMINE_DIR) CC=gcc CFLAGS="-g -O2 -pipe"
	test -x $@

$(PROTOCOL_DIR)/.pinned:
	@mkdir -p $(BUILD_DIR)
	git clone https://github.com/Limine-Bootloader/limine-protocol.git $(PROTOCOL_DIR)
	git -C $(PROTOCOL_DIR) checkout --detach $(LIMINE_PROTOCOL_COMMIT)
	@touch $@

$(BUILD_DIR)/main.o: kernel/src/main.c kernel/linker.ld kernel/src/console.h kernel/src/memory.h kernel/src/interrupts.h kernel/src/scheduler.h $(PROTOCOL_DIR)/.pinned
	$(CC) $(CFLAGS) -Ikernel/src -I$(PROTOCOL_DIR)/include -c $< -o $@

$(BUILD_DIR)/console.o: kernel/src/console.c kernel/src/console.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/src -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/src/memory.c kernel/src/memory.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/src -I$(PROTOCOL_DIR)/include -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/src/scheduler.c kernel/src/scheduler.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/src -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/src/interrupts.c kernel/src/interrupts.h kernel/src/console.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/src -c $< -o $@

$(BUILD_DIR)/interrupt_stubs.o: kernel/src/interrupt_stubs.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@
	@file $@

kernel: $(KERNEL_ELF)

$(ISO_IMAGE): $(KERNEL_ELF) $(LIMINE_DIR)/limine limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	   -no-emul-boot -boot-load-size 4 -boot-info-table \
	   --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part \
	   --efi-boot-image --protective-msdos-label $(ISO_ROOT) -o $@
	$(LIMINE_DIR)/limine bios-install $@

image: $(ISO_IMAGE)

run: $(ISO_IMAGE)
	qemu-system-x86_64 -machine q35 -m 512M -cdrom $(ISO_IMAGE) -serial stdio

test: $(ISO_IMAGE)
	bash scripts/smoke-test.sh

clean:
	rm -rf $(BUILD_DIR)
