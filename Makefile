ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
	CC = gcc
	LD = ld
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -m64 -march=x86-64 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mgeneral-regs-only
else
	CC = aarch64-linux-gnu-gcc
	LD = aarch64-linux-gnu-ld
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -march=armv8-a -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-pic -mgeneral-regs-only -mstrict-align
endif

BUILD = build/$(ARCH)
CFLAGS += -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/arch/$(ARCH)

OBJS_ARCH = \
	$(BUILD)/boot.o \
	$(BUILD)/switch.o \
	$(BUILD)/idt.o \
	$(BUILD)/pci.o \
	$(BUILD)/rtc.o \
	$(BUILD)/sysinfo.o \
	$(BUILD)/pmm.o \
	$(BUILD)/vmm.o

ifeq ($(ARCH),aarch64)
OBJS_ARCH += $(BUILD)/rpi_fb.o
endif

OBJS_CORE = \
	$(BUILD)/thread.o \
	$(BUILD)/mutex.o \
	$(BUILD)/kernel.o \
	$(BUILD)/mem.o \
	$(BUILD)/ramstat.o \
	$(BUILD)/console.o \
	$(BUILD)/virtio_net.o \
	$(BUILD)/net.o \
	$(BUILD)/ip.o \
	$(BUILD)/udp.o \
	$(BUILD)/dns.o \
	$(BUILD)/tcp.o \
	$(BUILD)/http.o \
	$(BUILD)/llm.o \
	$(BUILD)/fs.o \
	$(BUILD)/virtio_blk.o \
	$(BUILD)/srcfs.o \
	$(BUILD)/boot_gate.o \
	$(BUILD)/nano.o \
	$(BUILD)/agent.o \
	$(BUILD)/shell.o \
	$(BUILD)/httpd.o \
	$(BUILD)/fb.o \
	$(BUILD)/gui_font.o \
	$(BUILD)/mouse.o \
	$(BUILD)/gui.o \
	$(BUILD)/browser.o \
	$(BUILD)/image.o \
	$(BUILD)/imgview.o \
	$(BUILD)/fm.o \
	$(BUILD)/notepad.o \
	$(BUILD)/memview.o $(BUILD)/js_runtime.o \
	$(BUILD)/layout.o \
	$(BUILD)/paint.o

OBJS = $(OBJS_ARCH) $(OBJS_CORE)
HEADERS = $(wildcard src/*.h) $(wildcard src/*/*.h) $(wildcard src/cursors/*.h) $(wildcard src/fonts/*.h) $(wildcard src/icons/*.h) $(wildcard src/arch/$(ARCH)/*.h)

all: $(BUILD)/soma.iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/arch/$(ARCH)/boot.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/switch.o: src/arch/$(ARCH)/switch.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/arch/$(ARCH)/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/core/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/drivers/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/fs/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/net/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/ui/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/apps/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/agent/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/soma.elf: $(OBJS) linker_$(ARCH).ld
	$(LD) -T linker_$(ARCH).ld -o $@ $(OBJS)

$(BUILD)/soma.iso: $(BUILD)/soma.elf
ifeq ($(ARCH),x86_64)
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(BUILD)/soma.elf $(BUILD)/isodir/boot/soma.elf
	cp grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/isodir
	grub-file --is-x86-multiboot $(BUILD)/soma.elf
	cp $@ build/soma.iso
	cp $(BUILD)/soma.elf build/soma.elf
else
	@echo "ISO not applicable for aarch64. Target is bare ELF."
	touch $@
	cp $(BUILD)/soma.elf build/soma.elf
endif

clean:
	rm -rf build

.PHONY: all clean
