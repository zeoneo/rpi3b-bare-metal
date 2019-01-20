#include <device/usb.h>
#include <plibc/stdio.h>
#include <stdint.h>
#include <kernel/rpi-mailbox-interface.h>
#include <kernel/systimer.h>

extern void dmb(void);
uint32_t usb_hcd_device_id = 0x3;

void turn_off_usb()
{
    uint32_t turn_off_usb = 0x00;
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_POWER_STATE, usb_hcd_device_id, turn_off_usb);
    RPI_PropertyProcess();
    RPI_PropertyGet(TAG_SET_POWER_STATE);
}

void turn_on_usb()
{
    uint32_t turn_on_usb = 0x01;
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_POWER_STATE, usb_hcd_device_id, turn_on_usb);
    RPI_PropertyProcess();
    RPI_PropertyGet(TAG_SET_POWER_STATE);
}

void print_usb_power_state()
{

    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_GET_POWER_STATE, usb_hcd_device_id);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_GET_POWER_STATE);

    printf("\n device id: %x", (uint32_t)(mp->data.buffer_32[0]));
    printf("\n device state: %x", (uint32_t)(mp->data.buffer_32[1]));
}

int power_on_host_controller()
{
    turn_on_usb();
    print_usb_power_state();
    uint32_t *vendor_id = (void *)(USB_CORE_BASE + RegVendorId);
    uint32_t *user_id = (void *)(USB_CORE_BASE + RegUserId);

    if ((*vendor_id & 0xfffff000) != POWER_ON_VENDOR_ID)
    {
        printf("\n Host controller with expected vendor id not found. vendor_id: %x ", *vendor_id);
        return -1;
    }
    if (*user_id != POWER_ON_USB_USER_ID)
    {
        printf("\n Host controller with expected user id not found. user_id: %x ", *user_id);
        return -1;
    }
    return 0;
}

uint64_t tick_difference(uint64_t us1, uint64_t us2)
{
    if (us1 > us2)
    {                                       // If timer one is greater than two then timer rolled
        uint64_t td = UINT64_MAX - us1 + 1; // Counts left to roll value
        return us2 + td;                    // Add that to new count
    }
    return us2 - us1; // Return difference between values
}

int hcd_reset(void)
{
    uint64_t original_tick;

    struct CoreReset *core_reset = (void *)(USB_CORE_BASE + RegReset);
    original_tick = timer_getTickCount64(); // Hold original tickcount
    do
    {
        if (tick_difference(original_tick, timer_getTickCount64()) > 100000)
        {
            printf(" hcd reset timeout 1");
            return -1; // Return timeout error
        }
    } while (core_reset->AhbMasterIdle == false); // Keep looping until idle or timeout

    core_reset->CoreSoft = true; // Reset the soft core

    volatile struct CoreReset *temp;
    original_tick = timer_getTickCount64(); // Hold original tickcount
    do
    {
        if (tick_difference(original_tick, timer_getTickCount64()) > 100000)
        {
            printf(" hcd reset timeout");
            return -1; // Return timeout error
        }
        temp = core_reset;                                            // Read reset register
    } while (temp->CoreSoft == true || temp->AhbMasterIdle == false); // Keep looping until soft reset low/idle high or timeout

    printf("success hcd reset");
    return 0; // Return success
}

int hcd_start()
{
    struct UsbControl *coreUsb = (void *)(USB_CORE_BASE + RegUsb);
    coreUsb->UlpiDriveExternalVbus = 0;
    coreUsb->TsDlinePulseEnable = 0;
    if (hcd_reset() != 0)
    {
        printf("error while hcd reset");
    }
    else
    {
        printf("success hcd reset");
    }
    return 0;
}
void usb_initialise()
{
    power_on_host_controller();
    hcd_start();
}