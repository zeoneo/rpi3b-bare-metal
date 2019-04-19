#include <device/mouse.h>
#include <device/usb_report.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>

#define MouseMaxMice 4

uint32_t mouseCount __attribute__((aligned(4))) = 0;
uint32_t mouseAddresses[MouseMaxMice] = { 0, 0, 0, 0 };
struct UsbDevice* mice[MouseMaxMice];

void MouseLoad() 
{
	printf("CSUD: Mouse driver version 0.1\n"); 
	mouseCount = 0;
	for (uint32_t i = 0; i < MouseMaxMice; i++)
	{
		mouseAddresses[i] = 0;
		mice[i] = NULL;
	}
	HidUsageAttach[DesktopMouse] = MouseAttach;
}

uint32_t MouseIndex(uint32_t address) {
	if (address == 0) return 0xffffffff;

	for (uint32_t i = 0; i < mouseCount; i++)
		if (mouseAddresses[i] == address)
			return i;

	return 0xffffffff;
}

uint32_t MouseGetAddress(uint32_t index) {
	if (index > mouseCount) return 0;

    // for (uint32_t i = 0; index >= 0 && i < MouseMaxMice; i++) {
	for (uint32_t i = 0; i < MouseMaxMice; i++) {
		if (mouseAddresses[i] != 0)
			if (index-- == 0)
				return mouseAddresses[i];
	}

	return 0;
}

void MouseDetached(struct UsbDevice *device) {
	struct MouseDevice *data;
	
	data = (struct MouseDevice*)((struct HidDevice*)device->DriverData)->DriverData;
	if (data != NULL) {
		if (mouseAddresses[data->Index] == device->Number) {
			mouseAddresses[data->Index] = 0;
			mouseCount--;
			mice[data->Index] = NULL;
		}
	}
}

void MouseDeallocate(struct UsbDevice *device) {
	struct MouseDevice *data;
	
	data = (struct MouseDevice*)((struct HidDevice*)device->DriverData)->DriverData;
	if (data != NULL) {
		MemoryDeallocate(data);
		((struct HidDevice*)device->DriverData)->DriverData = NULL;
	}
	((struct HidDevice*)device->DriverData)->HidDeallocate = NULL;
	((struct HidDevice*)device->DriverData)->HidDetached = NULL;
}

Result MouseAttach(struct UsbDevice *device,  __attribute__((__unused__)) uint32_t interface) {
	uint32_t mouseNumber;
	struct HidDevice *hidData;
	struct MouseDevice *data;
	struct HidParserResult *parse;

	printf("\n Mouse Attach is called... \n ");
	if ((MouseMaxMice & 3) != 0) {
		printf("MOUSE: Warning! MouseMaxMice not a multiple of 4. The driver wasn't built for this!\n");
	}
	if (mouseCount == MouseMaxMice) {
		printf("MOUSE: %s not connected. Too many mice connected (%d/%d). Change MouseMaxMice in device.mouse.c to allow more.\n", UsbGetDescription(device), mouseCount, MouseMaxMice);
		return ErrorIncompatible;
	}
	
	hidData = (struct HidDevice*)device->DriverData;
	if (hidData->Header.DeviceDriver != DeviceDriverHid) {
		printf("MOUSE: %s isn't a HID device. The mouse driver is built upon the HID driver.\n", UsbGetDescription(device));
		return ErrorIncompatible;
	}

	parse = hidData->ParserResult;
	if (parse->Application.Page != GenericDesktopControl ||
		parse->Application.Desktop != DesktopMouse) {
		printf("MOUSE: %s doesn't seem to be a mouse...\n", UsbGetDescription(device));
		return ErrorIncompatible;
	}
	if (parse->ReportCount < 1) {
		printf("MOUSE: %s doesn't have enough outputs to be a mouse.\n", UsbGetDescription(device));
		return ErrorIncompatible;
	}
	hidData->HidDetached = MouseDetached;
	hidData->HidDeallocate = MouseDeallocate;
	
	if ((hidData->DriverData = MemoryAllocate(sizeof(struct MouseDevice))) == NULL) {
		printf("MOUSE: Not enough memory to allocate mouse %s.\n", UsbGetDescription(device));
		return ErrorMemory;
	}
	data = (struct MouseDevice*)hidData->DriverData;
	data->Header.DeviceDriver = DeviceDriverMouse;
	data->Header.DataSize = sizeof(struct MouseDevice);
	data->Index = mouseNumber = 0xffffffff;
	for (uint32_t i = 0; i < MouseMaxMice; i++) {
		if (mouseAddresses[i] == 0) {
			data->Index = mouseNumber = i;
			mouseAddresses[i] = device->Number;
			mouseCount++;
			break;
		}
	}

	printf("MOUSE: data->Index: %d", data->Index);
	if (mouseNumber == 0xffffffff) {
		printf("MOUSE: PANIC! Driver in inconsistent state! MouseCount is inaccurate.\n");
		MouseDeallocate(device);
		return ErrorGeneral;
	}

	mice[mouseNumber] = device;

	for (uint32_t i = 0; i < parse->ReportCount; i++) {
		if (parse->Report[i]->Type == Input 
//		    && data->MouseReport == NULL
		) {
			printf("MOUSE: Output report %d. %d fields.\n", i, parse->Report[i]->FieldCount);
			data->MouseReport = parse->Report[i];
			printf("MOUSE_PRAKASH: data->MouseReport->Index: %d", data->MouseReport->Index);
		} 
	}

	data->mouseX = 0;
	data->mouseY = 0;
	data->wheel = 0;
	data->buttonState = 0;

	printf("MOUSE: New mouse assigned %d!\n", device->Number);

	return OK;
}

uint32_t MouseCount() {
	return mouseCount;
}

Result MousePoll(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	Result result;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);	
	if (mouseNumber == 0xffffffff) return ErrorDisconnected;
	data = (struct MouseDevice*)(((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData);
	printf("MOUSE: Mouse report index: %d \n" , data->MouseReport->Index);
	if ((result = HidReadDevice(mice[mouseNumber], data->MouseReport->Index)) != OK) {
		if (result != ErrorDisconnected)
			printf("MOUSE: Could not get mouse report from %s.\n", UsbGetDescription(mice[mouseNumber]));
		return result;
	}

	struct HidParserReport *MouseReport = data->MouseReport;
	if (MouseReport->Type == Input) {
		// XXX: I'm sure I should be using HidGetFieldValue()
		// But this was so terribly easy.
		uint8_t *ReportBuffer = MouseReport->ReportBuffer;
		data->buttonState = (uint8_t)ReportBuffer[0];
		data->mouseX += (int8_t)ReportBuffer[1];
		data->mouseY += (int8_t)ReportBuffer[2];
		data->wheel += (int8_t)ReportBuffer[3];
		printf("buttonState :%d \n", data->buttonState);
		if (data->mouseX < 0) {
			data->mouseX = 0;
		}
		if (data->mouseY < 0) {
			data->mouseY = 0;
		}
	}

	return OK;
}

int16_t MouseGetPositionX(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;
	return data->mouseX;
}

int16_t MouseGetPositionY(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;
	return data->mouseY;
}

int16_t MouseGetWheel(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;
	return data->wheel;
}

uint32_t MouseGetPosition(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;
	return (data->mouseX << 16) | (data->mouseY & 0xFFFF);  /* x is high 16 bits; y is low 16 bits */
}

uint8_t MouseGetButtons(uint32_t mouseAddress) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;
	return data->buttonState;
}

bool MouseGetButtonIsPressed(uint32_t mouseAddress, enum MouseDeviceButton button) {
	uint32_t mouseNumber;
	struct MouseDevice *data;
	
	mouseNumber = MouseIndex(mouseAddress);
	if (mouseNumber == 0xffffffff) return 0;
	data = (struct MouseDevice*)((struct HidDevice*)mice[mouseNumber]->DriverData)->DriverData;

	switch (button) {
		case MouseDeviceButtonLeft:
			return (data->buttonState & 0x01);
		case MouseDeviceButtonRight:
			return (data->buttonState & 0x02);
		case MouseDeviceButtonMiddle:
			return (data->buttonState & 0x04);
		case MouseDeviceButtonSide:
			return (data->buttonState & 0x08);
		case MouseDeviceButtonExtra:
			return (data->buttonState & 0x10);
	}
	return false;
}
