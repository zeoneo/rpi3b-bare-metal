#include <device/usb.h>
#include <plibc/stdio.h>
#include <stdint.h>
#include <kernel/rpi-mailbox-interface.h>

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

void power_on_host_controller()
{
    turn_on_usb();
    print_usb_power_state();
}

void usb_initialise()
{
    power_on_host_controller();
    uint32_t *vendor_id = (void *)(USB_CORE_BASE + RegVendorId);
    uint32_t *user_id = (void *)(USB_CORE_BASE + RegUserId);

    if ((*vendor_id & 0xfffff000) == POWER_ON_VENDOR_ID)
    {
        printf("\n Host controller with expected vendor id found.");
    }
    if (*user_id == POWER_ON_USB_USER_ID)
    {
        printf("\n Host controller with expected user id found.");
    }
}