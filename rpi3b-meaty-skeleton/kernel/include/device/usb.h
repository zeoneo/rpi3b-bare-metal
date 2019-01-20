#ifndef USB_H
#define USB_H
#include <kernel/rpi-base.h>

#define POWER_ON_USB_USER_ID 0x2708A000 // from manual
#define POWER_ON_VENDOR_ID 0x4f542000   // rpi3b 4f54280a
#define USB_CORE_OFFSET 0x980000        // PAGE 202 BCM2835 manual

#define USB_CORE_BASE PERIPHERAL_BASE + USB_CORE_OFFSET

//Taken from CSUD https://github.com/Chadderz121/csud.git
enum CoreRegisters
{
    RegOtgControl = 0x0,
    RegOtgInterrupt = 0x4,
    RegAhb = 0x8,
    RegUsb = 0xc,
    RegReset = 0x10,
    RegInterrupt = 0x14,
    RegInterruptMask = 0x18,
    RegReceivePeek = 0x1c,
    RegReceivePop = 0x20,
    RegReceiveSize = 0x24,
    RegNonPeriodicFifoSize = 0x28,
    RegNonPeriodicFifoStatus = 0x2c,
    RegI2cControl = 0x30,
    RegPhyVendorControl = 0x34,
    RegGpio = 0x38,
    RegUserId = 0x3c,
    RegVendorId = 0x40,
    RegHardware = 0x44,
    RegLowPowerModeConfiguation = 0x48,
    RegMdioControl = 0x80, // 2835
    RegMdioRead = 0x84,    // 2835
    RegMdioWrite = 0x84,   // 2835
    RegMiscControl = 0x88, // 2835
    RegPeriodicFifoSize = 0x100,
    RegPeriodicFifoBase = 0x104,
    RegPower = 0xe00,
};

void usb_initialise();

#endif