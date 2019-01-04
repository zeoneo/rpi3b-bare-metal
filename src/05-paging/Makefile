COPTS := -march=armv8-a+crc -mfpu=crypto-neon-fp-armv8 -fpic -ffreestanding -Wextra -g
all:
	arm-none-eabi-gcc $(COPTS) -c boot.S -o boot.o
	arm-none-eabi-gcc $(COPTS) -c kernel.c -o kernel.o -O2 -Wall -Wextra
	arm-none-eabi-gcc $(COPTS) -c uart0.c -o uart0.o -O2 -Wall -Wextra
	arm-none-eabi-gcc $(COPTS) -c bss-clear.c -o bss-clear.o -O2 -Wall -Wextra
#	arm-none-eabi-gcc $(COPTS) -c rpi-armtimer.c -o rpi-armtimer.o
	arm-none-eabi-gcc $(COPTS) -c rpi-interrupts.c -o rpi-interrupts.o
	arm-none-eabi-gcc $(COPTS) -c virtmem.c -o virtmem.o
	# arm-none-eabi-gcc $(COPTS) -c rpi-systimer.c -o rpi-systimer.o
	arm-none-eabi-gcc -T linker.ld -o kernel8-32.elf -ffreestanding -O2 -nostdlib boot.o uart0.o kernel.o bss-clear.o rpi-interrupts.o virtmem.o
	arm-none-eabi-nm  kernel8-32.elf  > kernel8-32.map
	arm-none-eabi-objdump -D kernel8-32.elf > kernel8-32.list
	arm-none-eabi-objcopy kernel8-32.elf -O binary kernel8-32.img

qemu-wait:
	qemu-system-aarch64 -s -S -m 1024M -M raspi2 -serial stdio -kernel kernel8-32.elf

run: all
	qemu-system-aarch64 -m 1024M -M raspi2 -serial stdio -kernel kernel8-32.elf

debug:
	arm-none-eabi-gdb -ex "target remote localhost:1234" -ex "symbol-file kernel8-32.elf"

clean:
	rm -f *.o *.img *.elf *.list *.map