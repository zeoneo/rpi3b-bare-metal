all:
	arm-none-eabi-gcc -march=armv8-a+crc -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding -c boot.S -o boot.o
	arm-none-eabi-gcc -march=armv8-a+crc -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding -std=gnu99 -c kernel.c -o kernel.o -O2 -Wall -Wextra
	arm-none-eabi-gcc -T linker.ld -o myos.elf -ffreestanding -O2 -nostdlib boot.o kernel.o
	arm-none-eabi-objcopy myos.elf -O binary kernel8-32.img

clean:
	rm -f *.o *.img *.elf