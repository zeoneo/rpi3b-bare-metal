#ifndef USB_H
#define USB_H
#include <kernel/rpi-base.h>
#include <stdbool.h>

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

struct __attribute__((__packed__, aligned(4))) UsbControl
{
    union {
        struct __attribute__((__packed__, aligned(1)))
        {
            volatile unsigned toutcal : 3;  // @0
            volatile bool PhyInterface : 1; // @3
            volatile enum UMode {
                ULPI,
                UTMI,
            } ModeSelect : 1;                          // @4
            volatile bool fsintf : 1;                  // @5
            volatile bool physel : 1;                  // @6
            volatile bool ddrsel : 1;                  // @7
            volatile bool SrpCapable : 1;              // @8
            volatile bool HnpCapable : 1;              // @9
            volatile unsigned usbtrdtim : 4;           // @10
            volatile unsigned reserved1 : 1;           // @14
            volatile bool phy_lpm_clk_sel : 1;         // @15
            volatile bool otgutmifssel : 1;            // @16
            volatile bool UlpiFsls : 1;                // @17
            volatile bool ulpi_auto_res : 1;           // @18
            volatile bool ulpi_clk_sus_m : 1;          // @19
            volatile bool UlpiDriveExternalVbus : 1;   // @20
            volatile bool ulpi_int_vbus_indicator : 1; // @21
            volatile bool TsDlinePulseEnable : 1;      // @22
            volatile bool indicator_complement : 1;    // @23
            volatile bool indicator_pass_through : 1;  // @24
            volatile bool ulpi_int_prot_dis : 1;       // @25
            volatile bool ic_usb_capable : 1;          // @26
            volatile bool ic_traffic_pull_remove : 1;  // @27
            volatile bool tx_end_delay : 1;            // @28
            volatile bool force_host_mode : 1;         // @29
            volatile bool force_dev_mode : 1;          // @30
            volatile unsigned _reserved31 : 1;         // @31
        };
        volatile uint32_t Raw32; // Union to access all 32 bits as a uint32_t
    };
};

struct __attribute__((__packed__, aligned(4))) CoreReset
{
    union {
        struct __attribute__((__packed__, aligned(1)))
        {
            volatile bool CoreSoft : 1;                    // @0
            volatile bool HclkSoft : 1;                    // @1
            volatile bool HostFrameCounter : 1;            // @2
            volatile bool InTokenQueueFlush : 1;           // @3
            volatile bool ReceiveFifoFlush : 1;            // @4
            volatile bool TransmitFifoFlush : 1;           // @5
            volatile unsigned TransmitFifoFlushNumber : 5; // @6
            volatile unsigned _reserved11_29 : 19;         // @11
            volatile bool DmaRequestSignal : 1;            // @30
            volatile bool AhbMasterIdle : 1;               // @31
        };
        volatile uint32_t Raw32; // Union to access all 32 bits as a uint32_t
    };
};

void usb_initialise();

#endif