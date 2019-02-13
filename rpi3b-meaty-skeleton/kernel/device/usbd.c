#include <device/usbd.h>
#include <device/hcd.h>
#include <device/usb-mem.h>
#include <plibc/stdio.h>

Result UsbInitialise()
{
    Result result = OK;

    // Init usb mem allocation
    PlatformLoad();

    if (sizeof(struct UsbDeviceRequest) != 0x8)
    {
        printf("USBD: Incorrectly compiled driver. UsbDeviceRequest: %#x (0x8).\n",
               sizeof(struct UsbDeviceRequest));
        return ErrorCompiler; // Correct packing settings are required.
    }
    else
    {
        printf("USBD: Size of usb device request :%d", sizeof(struct UsbDeviceRequest));
    }

    if ((result = HcdInitialize()) != OK)
    {
        printf("USBD: Abort, HCD failed to initialise.\n");
        return result;
    }

    HcdStart();
    return OK;
}