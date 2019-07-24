CFLAGS?=-O2 -g -march=armv8-a+crc -mcpu=cortex-a53 -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only
CPPFLAGS?=
LDFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -nostdinc -ffreestanding -Wall -Wextra -Werror -Wno-sign-compare -std=c11  -mfpu=neon -nostartfiles -mno-unaligned-access -fno-tree-loop-vectorize -fno-tree-slp-vectorize -Wno-nonnull-compare
CPPFLAGS:=$(CPPFLAGS) -D__is_kernel -I/Users/prakashborkar/Desktop/Remote_Repo/rpi-os-uart/rpi3b-meaty-skeleton/sysroot/usr/include
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lk -lm

ARCHDIR=arch/$(HOSTARCH)
MEMDIR=mem
DEVICEDIR=device
FSDIR=fs
GRAPHICSDIR=graphics
KLIBDIR=klib

include $(ARCHDIR)/make.config
include $(MEMDIR)/make.config
include $(DEVICEDIR)/make.config
include $(FSDIR)/make.config
include $(KLIBDIR)/make.config


CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
$(KERNEL_MEM_OBJS) \
$(KERNEL_DEVICE_OBJS) \
$(KERNEL_FS_OBJS) \
$(K_LIB_OBJS) \
kernel/kernel.o

OBJS= $(KERNEL_OBJS)

LINK_LIST=\
$(LDFLAGS) \
$(KERNEL_OBJS) \
$(LIBS) \

.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .S

all: kernel8-32.img

kernel8-32.img:kernel8-32.elf
	arm-none-eabi-objdump -D kernel8-32.elf > kernel8-32.list
	arm-none-eabi-nm --demangle -gn --defined-only kernel8-32.elf  > kernel8-32.map
	arm-none-eabi-objcopy kernel8-32.elf -O binary kernel8-32.img

kernel8-32.elf: $(OBJS) $(ARCHDIR)/linker.ld
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f kernel8-32.elf kernel8-32.img
	rm -f $(OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d
	rm -f *.list *.map *.txt

install: install-kernel

install-kernel: kernel8-32.img
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp kernel8-32.img $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
