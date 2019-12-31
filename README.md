# rpi3b-bare-metal   [![Build Status](https://travis-ci.com/zeoneo/rpi3b-bare-metal.svg?branch=master)](https://travis-ci.com/zeoneo/rpi3b-bare-metal)



This tutorial supports only Raspberry Pi 3 B model and tutorial is target at 32 bits.

I am going to attempt bare metal programming on Raspberry Pi 3 B.

1. Get UART0 working.
2. Get ARM Timer working.
3. Get Interrupt working.
4.

## How to COMPILE code.

### For individual lessons:
- Change directory to src/lesson-dir
- Invoke `make` and it will build kernel8-32.img binary (This needs compiler arm-none-eabi-gcc compiler download it from [here](https://armkeil.blob.core.windows.net/developer//sitecore/shell/-/media/Files/downloads/gnu-rm/5_4-2016q3/gcc-arm-none-eabi-5_4-2016q3-20160926-linux,-d-,tar.bz2])


### For complete OS (using local compiler)
- Change directory to `rpi3b-meaty-skeleton`
- Invoke `./build.sh`
- Find binary in kernel folder.

### For complete OS (using DOCKER IMAGE)
- Get docker desktop for your host operating system
- Change directory to `rpi3b-meaty-skeleton`
- Invoke `./docker-build.sh`
- Find binary in kernel folder.





It's important to check files in Disk Image folder. Disk Image is ideal setup for lessons to work.

