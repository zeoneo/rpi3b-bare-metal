CC = arm-none-eabi-gcc --sysroot=/Users/prakashborkar/Desktop/Remote_Repo/rpi-os-uart/rpi3b-meaty-skeleton/sysroot
AS = arm-none-eabi-as
ASFLAGS = -mcpu=cortex-a53

CFLAGS = -O2 -Wall -g -march=armv8-a+crc -mcpu=cortex-a53 -mfpu=crypto-neon-fp-armv8 -fpic \
-marm -nostartfiles -ffreestanding -nostdlib \
-nostdinc -ffreestanding -Wall -Wextra -Werror -Wno-sign-compare -std=c11 \
-mfpu=neon -nostartfiles -mno-unaligned-access \
-fno-tree-loop-vectorize -fno-tree-slp-vectorize -Wno-nonnull-compare \
-I/Users/prakashborkar/Desktop/Remote_Repo/rpi-os-uart/rpi3b-meaty-skeleton/sysroot/usr/include

OBJCOPYFLAGS = -R .note -R .comment -R .note.gnu.build-id
LDFLAGS?=
LIBS?=
LIBS:=$(LIBS) -nostdlib -lk

LINK_LIST=\
$(LDFLAGS) \
start.o \
hello.o \
$(LIBS) \

GENROMFS = genromfs

.PHONY: all clean
.SUFFIXES: .o .c .S

all: install

install: initrd_image
	mv ./initrd.h $(DESTDIR)/usr/include/

initrd_image: hello.elf
	mkdir -p image
	mkdir -p image/bin/prak/kernel/home/folder
	mkdir -p image/home
	mkdir -p image/home/ansis
	mkdir -p image/proc
	mkdir -p image/etc
	mkdir -p image/src
	cp hello.elf ./image/bin/
	$(GENROMFS) -f initrd.romfs -d image -V Neonix
	echo "unsigned char initrd_image[]={" > initrd.h
	xxd -i < initrd.romfs >> initrd.h
	echo "};" >> initrd.h

hello.elf: start.o hello.o ./linker.ld
	$(CC) -T ./linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	arm-none-eabi-objdump -D hello.elf >  hello.list
	arm-none-eabi-nm --demangle -gn --defined-only hello.elf  > hello.map

.c.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS) $(ASFLAGS)

clean:
	rm -f *.elf *.img
	rm -f *.o */*.o */*/*.o
	rm -f *.d */*.d */*/*.d
	rm -f *.list *.map *.txt
	rm -rf image/
	rm -f initrd.h initrd.romfs