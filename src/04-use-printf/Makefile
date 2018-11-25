COPTS := -march=armv8-a+crc -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding
all:
	arm-none-eabi-gcc $(COPTS) -c boot.S -o boot.o 
	arm-none-eabi-gcc $(COPTS) -c kernel.c -o kernel.o -O2 -Wall -Wextra
	arm-none-eabi-gcc $(COPTS) -c uart0.c -o uart0.o -O2 -Wall -Wextra
	arm-none-eabi-gcc $(COPTS) -c bss-clear.c -o bss-clear.o -O2 -Wall -Wextra
	arm-none-eabi-gcc $(COPTS) -c rpi-armtimer.c -o rpi-armtimer.o
	arm-none-eabi-gcc $(COPTS) -c rpi-interrupts.c -o rpi-interrupts.o
	arm-none-eabi-gcc $(COPTS) -c string.c -o string.o
	arm-none-eabi-gcc $(COPTS) -c rpi-mailbox.c -o rpi-mailbox.o
	arm-none-eabi-gcc $(COPTS) -c rpi-mailbox-interface.c -o rpi-mailbox-interface.o
	arm-none-eabi-gcc $(COPTS) -c syscalls.c -o syscalls.o


	# arm-none-eabi-gcc $(COPTS) -c rpi-systimer.c -o rpi-systimer.o
	arm-none-eabi-gcc -T linker.ld -o myos.elf -ffreestanding -O2 -nostdlib boot.o uart0.o kernel.o bss-clear.o \
	rpi-armtimer.o rpi-interrupts.o   string.o rpi-mailbox-interface.o rpi-mailbox.o syscalls.o -lc -lgcc

	arm-none-eabi-objcopy myos.elf -O binary kernel8-32.img

clean:
	rm -f *.o *.img *.elf