#ifndef USB_H
#define USB_H

#include<kernel/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct __attribute__((__packed__))  UsbDeviceRequest {
	uint8_t Type; // +0x0
	enum UsbDeviceRequestRequest {
		// USB requests
		GetStatus = 0,
		ClearFeature = 1,
		SetFeature = 3,
		SetAddress = 5,
		GetDescriptor = 6,
		SetDescriptor = 7,
		GetConfiguration = 8,
		SetConfiguration = 9,
		GetInterface = 10,
		SetInterface = 11,
		SynchFrame = 12,
		// HID requests
		GetReport = 1,
		GetIdle = 2,
		GetProtocol = 3,
		SetReport = 9,
		SetIdle = 10,
		SetProtocol = 11,
	} Request : 8; // +0x1
	uint16_t Value; // +0x2
	uint16_t Index; // +0x4
	uint16_t Length; // +0x6
};

Result UsbInitialise();

#ifdef __cplusplus
}
#endif

#endif