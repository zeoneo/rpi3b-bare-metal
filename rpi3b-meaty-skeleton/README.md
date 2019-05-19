# RPI 3B (32 BIT) OS [![Build Status](https://travis-ci.com/zeoneo/rpi3b-bare-metal.svg?branch=master)](https://travis-ci.com/zeoneo/rpi3b-bare-metal)

Initial setup taken from osdev meaty-skeleton

I am trying to come up with RPI os. individual tutorials are in src folder.

I am using src/00-bootloader to test my kernel. it is based on raspbootin. please search github raspbootin for more info.


As of today (May 19, 2019) this repo only contains device drivers which are essential for any operating system.

Completed Device Drivers:
 - UART
 - EMMC
 - DMA
 - USB Mouse
 - USB Keyboard
 - Frame buffer
 - FAT32 read driver
 - Set up Virtual Paging

In future I will add
 - GPIO driver
 - SPI
 - I2C
 - Bluetooth
 - LAN driver
 
Next task is to create user space. To dos
 - Add physical memory manager
 - Add virtual memory manager
 - Make sure all the other completed driver works with virtual memory manager
 - Go multi core and create scheduler
 - Create process
 - Write bare essential user space libraries
 - Add feature to read, load user program and execute it.
 
 
 Wish list:
  - Port gcc
  - Support for ext2 file system
  - Wifi (too ambitious ?)
  - Self hosting
  - Write/port editor
  - Write 1 or 2 games
 
 
 
