COPTS := -march=armv8-a+crc -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding -std=gnu99 -O2 -Wall -Wextra
LINKEROPTS := -ffreestanding -O2 -nostdlib

ELF := kernel8-32.elf
TARGET := kernel8-32.img
all:
	arm-none-eabi-as boot.S -o boot.o
	arm-none-eabi-gcc  $(COPTS) -c kernel.c -o kernel.o
	arm-none-eabi-gcc  $(COPTS) -c uart0.c -o uart0.o
	arm-none-eabi-gcc -T linker.ld -o $(ELF)  boot.o kernel.o uart0.o $(LINKEROPTS)
	arm-none-eabi-objcopy $(ELF) -O binary $(TARGET)

clean:
	rm -f *.o *.img *.elf