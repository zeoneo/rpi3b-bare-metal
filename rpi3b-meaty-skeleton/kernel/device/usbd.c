#include <kernel/systimer.h>
#include <kernel/types.h>

#include <device/hcd.h>
#include <device/usb-mem.h>
#include <plibc/stdio.h>
#include <device/usbd.h>

/** The default timeout in ms of control transfers. */
#define ControlMessageTimeout 10
/** The maximum number of devices that can be connected. */
#define MaximumDevices 32
#define InterfaceClassAttachCount 16
struct UsbDevice *Devices[MaximumDevices];
Result (*InterfaceClassAttach[InterfaceClassAttachCount])(struct UsbDevice *device, uint32_t interfaceNumber);

Result UsbControlMessage(struct UsbDevice *device,
						 struct UsbPipeAddress pipe, void *buffer, uint32_t bufferLength,
						 struct UsbDeviceRequest *request, uint32_t timeout)
{
	Result result;

	if (((uint32_t)buffer & 0x3) != 0)
		printf("USBD: Warning message buffer not word aligned.\n");
	result = HcdSumbitControlMessage(device, pipe, buffer, bufferLength, request);

	if (result != OK)
	{
		printf("USBD: Failed to send message to %s: %d.\n", UsbGetDescription(device), result);
		return result;
	}

	while (timeout-- > 0 && (device->Error & Processing))
	{
		// printf("Wating 10000 micros seconds.\n");
		uint64_t timer_tick = timer_getTickCount64();
		while (timer_getTickCount64() < (timer_tick + 10000))
			;
		// printf("Wating 10000 micros seconds completed .\n");
	}

	if ((device->Error & Processing))
	{
		printf("USBD: Message to %s timeout reached.\n", UsbGetDescription(device));
		return ErrorTimeout;
	}

	if (device->Error & ~Processing)
	{
		if (device->Parent != NULL && device->Parent->DeviceCheckConnection != NULL)
		{
			// Check we're still connected!
			printf("USBD: Verifying %s is still connected.\n", UsbGetDescription(device));
			if ((result = device->Parent->DeviceCheckConnection(device->Parent, device)) != OK)
			{
				return ErrorDisconnected;
			}
			printf("USBD: Yes, %s is still connected.\n", UsbGetDescription(device));
		}
		result = ErrorDevice;
	}

	return result;
}

Result UsbReadStringLang(struct UsbDevice *device, uint8_t stringIndex, uint16_t langId, void *buffer, uint32_t length)
{
	Result result;

	result = UsbGetString(device, stringIndex, langId, buffer, Min(2, length, uint32_t));

	if (result == OK && device->LastTransfer != length)
		result = UsbGetString(device, stringIndex, langId, buffer, Min(((uint8_t *)buffer)[0], length, uint32_t));

	return result;
}

Result UsbSetConfiguration(struct UsbDevice *device, uint8_t configuration)
{
	Result result;

	if (device->Status != Addressed)
	{
		printf("USBD: Illegal attempt to configure device %s in state %#x.\n", UsbGetDescription(device), device->Status);
		return ErrorDevice;
	}

	if ((result = UsbControlMessage(
			 device,
			 (struct UsbPipeAddress){
				 .Type = Control,
				 .Speed = device->Speed,
				 .EndPoint = 0,
				 .Device = device->Number,
				 .Direction = Out,
				 .MaxSize = SizeFromNumber(device->Descriptor.MaxPacketSize0),
			 },
			 NULL,
			 0,
			 &(struct UsbDeviceRequest){
				 .Request = SetConfiguration,
				 .Type = 0,
				 .Value = configuration,
			 },
			 ControlMessageTimeout)) != OK)
		return result;

	device->ConfigurationIndex = configuration;
	device->Status = Configured;
	return OK;
}
Result UsbGetDescriptor(struct UsbDevice *device, enum DescriptorType type,
						uint8_t index, uint16_t langId, void *buffer, uint32_t length, uint32_t minimumLength, uint8_t recipient)
{
	Result result;

	if ((result = UsbControlMessage(
			 device,
			 (struct UsbPipeAddress){
				 .Type = Control,
				 .Speed = device->Speed,
				 .EndPoint = 0,
				 .Device = device->Number,
				 .Direction = In,
				 .MaxSize = SizeFromNumber(device->Descriptor.MaxPacketSize0),
			 },
			 buffer,
			 length,
			 &(struct UsbDeviceRequest){
				 .Request = GetDescriptor,
				 .Type = 0x80 | recipient,
				 .Value = (uint16_t)type << 8 | index,
				 .Index = langId,
				 .Length = length,
			 },
			 ControlMessageTimeout)) != OK)
	{
		printf("USBD: Failed to get descriptor %#x:%#x for device %s. Result %#x.\n", type, index, UsbGetDescription(device), result);
		return result;
	}

	if (device->LastTransfer < minimumLength)
	{
		printf("USBD: Unexpectedly short descriptor (%d/%d) %#x:%#x for device %s. Result %#x.\n", device->LastTransfer, minimumLength, type, index, UsbGetDescription(device), result);
		return ErrorDevice;
	}

	return OK;
}

const char *UsbGetDescription(struct UsbDevice *device)
{
	if (device->Status == Attached)
		return "New Device (Not Ready)\0";
	else if (device->Status == Powered)
		return "Unknown Device (Not Ready)\0";
	else if (device == Devices[0])
		return "USB Root Hub\0";

	switch (device->Descriptor.Class)
	{
	case DeviceClassHub:
		if (device->Descriptor.UsbVersion == 0x210)
			return "USB 2.1 Hub\0";
		else if (device->Descriptor.UsbVersion == 0x200)
			return "USB 2.0 Hub\0";
		else if (device->Descriptor.UsbVersion == 0x110)
			return "USB 1.1 Hub\0";
		else if (device->Descriptor.UsbVersion == 0x100)
			return "USB 1.0 Hub\0";
		else
			return "USB Hub\0";
	case DeviceClassVendorSpecific:
		if (device->Descriptor.VendorId == 0x424 &&
			device->Descriptor.ProductId == 0xec00)
			return "SMSC LAN9512\0";
	case DeviceClassInInterface:
		if (device->Status == Configured)
		{
			switch (device->Interfaces[0].Class)
			{
			case InterfaceClassAudio:
				return "USB Audio Device\0";
			case InterfaceClassCommunications:
				return "USB CDC Device\0";
			case InterfaceClassHid:
				switch (device->Interfaces[0].Protocol)
				{
				case 1:
					return "USB Keyboard\0";
				case 2:
					return "USB Mouse\0";
				default:
					return "USB HID\0";
				}
			case InterfaceClassPhysical:
				return "USB Physical Device\0";
			case InterfaceClassImage:
				return "USB Imaging Device\0";
			case InterfaceClassPrinter:
				return "USB Printer\0";
			case InterfaceClassMassStorage:
				return "USB Mass Storage Device\0";
			case InterfaceClassHub:
				if (device->Descriptor.UsbVersion == 0x210)
					return "USB 2.1 Hub\0";
				else if (device->Descriptor.UsbVersion == 0x200)
					return "USB 2.0 Hub\0";
				else if (device->Descriptor.UsbVersion == 0x110)
					return "USB 1.1 Hub\0";
				else if (device->Descriptor.UsbVersion == 0x100)
					return "USB 1.0 Hub\0";
				else
					return "USB Hub\0";
			case InterfaceClassCdcData:
				return "USB CDC-Data Device\0";
			case InterfaceClassSmartCard:
				return "USB Smart Card\0";
			case InterfaceClassContentSecurity:
				return "USB Content Secuity Device\0";
			case InterfaceClassVideo:
				return "USB Video Device\0";
			case InterfaceClassPersonalHealthcare:
				return "USB Healthcare Device\0";
			case InterfaceClassAudioVideo:
				return "USB AV Device\0";
			case InterfaceClassDiagnosticDevice:
				return "USB Diagnostic Device\0";
			case InterfaceClassWirelessController:
				return "USB Wireless Controller\0";
			case InterfaceClassMiscellaneous:
				return "USB Miscellaneous Device\0";
			case InterfaceClassVendorSpecific:
				return "Vendor Specific\0";
			default:
				return "Generic Device\0";
			}
		}
		else if (device->Descriptor.Class == DeviceClassVendorSpecific)
			return "Vendor Specific\0";
		else
			return "Unconfigured Device\0";
	default:
		return "Generic Device\0";
	}
}

Result UsbConfigure(struct UsbDevice *device, uint8_t configuration)
{
	Result result;
	void *fullDescriptor;
	struct UsbDescriptorHeader *header;
	struct UsbInterfaceDescriptor *interface;
	struct UsbEndpointDescriptor *endpoint;
	uint32_t lastInterface, lastEndpoint;
	bool isAlternate;

	if (device->Status != Addressed)
	{
		printf("USBD: Illegal attempt to configure device %s in state %#x.\n", UsbGetDescription(device), device->Status);
		return ErrorDevice;
	}

	if ((result = UsbGetDescriptor(device, Configuration, configuration, 0, (void *)&device->Configuration, sizeof(device->Configuration), sizeof(device->Configuration), 0)) != OK)
	{
		printf("USBD: Failed to retrieve configuration descriptor %#x for device %s.\n", configuration, UsbGetDescription(device));
		return result;
	}

	if ((fullDescriptor = MemoryAllocate(device->Configuration.TotalLength)) == NULL)
	{
		printf("USBD: Failed to allocate space for descriptor.\n");
		return ErrorMemory;
	}
	if ((result = UsbGetDescriptor(device, Configuration, configuration, 0, fullDescriptor, device->Configuration.TotalLength, device->Configuration.TotalLength, 0)) != OK)
	{
		printf("USBD: Failed to retrieve full configuration descriptor %#x for device %s.\n", configuration, UsbGetDescription(device));
		goto deallocate;
	}

	device->ConfigurationIndex = configuration;
	configuration = device->Configuration.ConfigurationValue;

	header = fullDescriptor;
	lastInterface = MaxInterfacesPerDevice;
	lastEndpoint = MaxEndpointsPerDevice;
	isAlternate = false;

	for (header = (struct UsbDescriptorHeader *)((uint8_t *)header + header->DescriptorLength);
		 (uint32_t)header - (uint32_t)fullDescriptor < device->Configuration.TotalLength;
		 header = (struct UsbDescriptorHeader *)((uint8_t *)header + header->DescriptorLength))
	{
		switch (header->DescriptorType)
		{
		case Interface:
			interface = (struct UsbInterfaceDescriptor *)header;
			if (lastInterface != interface->Number)
			{
				MemoryCopy((void *)&device->Interfaces[lastInterface = interface->Number], (void *)interface, sizeof(struct UsbInterfaceDescriptor));
				lastEndpoint = 0;
				isAlternate = false;
			}
			else
				isAlternate = true;
			break;
		case Endpoint:
			if (isAlternate)
				break;
			if (lastInterface == MaxInterfacesPerDevice || lastEndpoint >= device->Interfaces[lastInterface].EndpointCount)
			{
				printf("USBD: Unexpected endpoint descriptor in %s.Interface%d.\n", UsbGetDescription(device), lastInterface + 1);
				break;
			}
			endpoint = (struct UsbEndpointDescriptor *)header;
			MemoryCopy((void *)&device->Endpoints[lastInterface][lastEndpoint++], (void *)endpoint, sizeof(struct UsbEndpointDescriptor));
			break;
		default:
			if (header->DescriptorLength == 0)
				goto headerLoopBreak;

			break;
		}

		printf("USBD: Descriptor %d length %d, interface %d.\n", header->DescriptorType, header->DescriptorLength, lastInterface);
	}
headerLoopBreak:

	if ((result = UsbSetConfiguration(device, configuration)) != OK)
	{
		goto deallocate;
	}
	printf("USBD: %s configuration %d. Class %d, subclass %d.\n", UsbGetDescription(device), configuration, device->Interfaces[0].Class, device->Interfaces[0].SubClass);

	device->FullConfiguration = fullDescriptor;

	return OK;
deallocate:
	MemoryDeallocate(fullDescriptor);
	return result;
}

Result UsbReadString(struct UsbDevice *device, uint8_t stringIndex, char *buffer, uint32_t length)
{
	Result result;
	uint32_t i;
	uint8_t descriptorLength;

	if (buffer == NULL || stringIndex == 0)
		return ErrorArgument;
	uint16_t langIds[2];
	struct UsbStringDescriptor *descriptor;

	result = UsbReadStringLang(device, 0, 0, &langIds, 4);

	if (result != OK)
	{
		printf("USBD: Error getting language list from %s.\n", UsbGetDescription(device));
		return result;
	}
	else if (device->LastTransfer < 4)
	{
		printf("USBD: Unexpectedly short language list from %s.\n", UsbGetDescription(device));
		return ErrorDevice;
	}

	descriptor = (struct UsbStringDescriptor *)buffer;
	if (descriptor == NULL)
		return ErrorMemory;
	if ((result = UsbReadStringLang(device, stringIndex, langIds[1], descriptor, length)) != OK)
		return result;

	descriptorLength = descriptor->DescriptorLength;
	for (i = 0; i < length - 1 && i < (uint32_t)((descriptorLength - 2) >> 1); i++)
	{
		if (descriptor->Data[i] > 0xff)
			buffer[i] = '?';
		else
		{
			buffer[i] = descriptor->Data[i];
		}
	};

	if (i < length)
		buffer[i++] = '\0';

	return result;
}

Result UsbReadDeviceDescriptor(struct UsbDevice *device)
{
	Result result;
	if (device->Speed == Low)
	{
		device->Descriptor.MaxPacketSize0 = 8;
		if ((result = UsbGetDescriptor(device, Device, 0, 0, (void *)&device->Descriptor, sizeof(device->Descriptor), 8, 0)) != OK)
			return result;
		if (device->LastTransfer == sizeof(struct UsbDeviceDescriptor))
			return result;
		return UsbGetDescriptor(device, Device, 0, 0, (void *)&device->Descriptor, sizeof(device->Descriptor), sizeof(device->Descriptor), 0);
	}
	else if (device->Speed == Full)
	{
		device->Descriptor.MaxPacketSize0 = 64;
		if ((result = UsbGetDescriptor(device, Device, 0, 0, (void *)&device->Descriptor, sizeof(device->Descriptor), 8, 0)) != OK)
			return result;
		if (device->LastTransfer == sizeof(struct UsbDeviceDescriptor))
			return result;
		return UsbGetDescriptor(device, Device, 0, 0, (void *)&device->Descriptor, sizeof(device->Descriptor), sizeof(device->Descriptor), 0);
	}
	else
	{
		device->Descriptor.MaxPacketSize0 = 64;
		return UsbGetDescriptor(device, Device, 0, 0, (void *)&device->Descriptor, sizeof(device->Descriptor), sizeof(device->Descriptor), 0);
	}
}

Result UsbSetAddress(struct UsbDevice *device, uint8_t address)
{
	Result result;

	if (device->Status != Default)
	{
		printf("USBD: Illegal attempt to configure device %s in state %#x.\n", UsbGetDescription(device), device->Status);
		return ErrorDevice;
	}

	if ((result = UsbControlMessage(
			 device,
			 (struct UsbPipeAddress){
				 .Type = Control,
				 .Speed = device->Speed,
				 .EndPoint = 0,
				 .Device = 0,
				 .Direction = Out,
				 .MaxSize = SizeFromNumber(device->Descriptor.MaxPacketSize0),
			 },
			 NULL,
			 0,
			 &(struct UsbDeviceRequest){
				 .Request = SetAddress,
				 .Type = 0,
				 .Value = address,
			 },
			 ControlMessageTimeout)) != OK)
		return result;

	printf("Wating 10000 micros seconds.\n");
	uint64_t timer_tick = timer_getTickCount64();
	while (timer_getTickCount64() < (timer_tick + 10000))
		;
	printf("Wating 10000 micros seconds completed .\n");

	device->Number = address;
	device->Status = Addressed;
	return OK;
}

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

Result UsbAttachRootHub()
{
	Result result;
	struct UsbDevice *rootHub;
	printf("USBD: Scanning for devices.\n");
	if (Devices[0] != NULL)
		UsbDeallocateDevice(Devices[0]);
	if ((result = UsbAllocateDevice(&rootHub)) != OK)
		return result;

	Devices[0]->Status = Powered;

	return UsbAttachDevice(Devices[0]);
}

Result UsbAllocateDevice(struct UsbDevice **device)
{
	if ((*device = MemoryAllocate(sizeof(struct UsbDevice))) == NULL)
		return ErrorMemory;

	for (uint32_t number = 0; number < MaximumDevices; number++)
	{
		if (Devices[number] == NULL)
		{
			Devices[number] = *device;
			(*device)->Number = number + 1;
			break;
		}
	}

	printf("USBD: Allocating new device, address %#x.\n", (*device)->Number);

	(*device)->Status = Attached;
	(*device)->Error = None;
	(*device)->PortNumber = 0;
	(*device)->Parent = NULL;
	(*device)->DriverData = NULL;
	(*device)->FullConfiguration = NULL;
	(*device)->ConfigurationIndex = 0xff;
	(*device)->DeviceDeallocate = NULL;
	(*device)->DeviceDetached = NULL;
	(*device)->DeviceCheckConnection = NULL;
	(*device)->DeviceCheckForChange = NULL;
	(*device)->DeviceChildDetached = NULL;
	(*device)->DeviceChildReset = NULL;
	return OK;
}

Result UsbAttachDevice(struct UsbDevice *device)
{
	Result result;
	uint8_t address;
	char *buffer;

	// Store the address until it is actually assigned.
	address = device->Number;
	device->Number = 0;
	printf("USBD: Scanning %d. %s.\n", address, SpeedToChar(device->Speed));

	if ((result = UsbReadDeviceDescriptor(device)) != OK)
	{
		printf("USBD: Failed to read device descriptor for %d.\n", address);
		device->Number = address;
		return result;
	}
	device->Status = Default;

	if (device->Parent != NULL && device->Parent->DeviceChildReset != NULL)
	{
		// Reset the port for what will be the second time.
		if ((result = device->Parent->DeviceChildReset(device->Parent, device)) != OK)
		{
			printf("USBD: Failed to reset port again for new device %s.\n", UsbGetDescription(device));
			device->Number = address;
			return result;
		}
	}

	if ((result = UsbSetAddress(device, address)) != OK)
	{
		printf("USBD: Failed to assign address to %#x.\n", address);
		device->Number = address;
		return result;
	}
	device->Number = address;

	if ((result = UsbReadDeviceDescriptor(device)) != OK)
	{
		printf("USBD: Failed to reread device descriptor for %#x.\n", address);
		return result;
	}

	printf("USBD: Attach Device %s. Address:%d Class:%d Subclass:%d USB:%x.%x. %d configurations, %d interfaces.\n",
		   UsbGetDescription(device), address, device->Descriptor.Class, device->Descriptor.SubClass, device->Descriptor.UsbVersion >> 8,
		   (device->Descriptor.UsbVersion >> 4) & 0xf, device->Descriptor.ConfigurationCount, device->Configuration.InterfaceCount);

	printf("USBD: Device Attached: %s.\n", UsbGetDescription(device));
	buffer = NULL;

	if (device->Descriptor.Product != 0)
	{
		if (buffer == NULL)
			buffer = MemoryAllocate(0x100);
		if (buffer != NULL)
		{
			result = UsbReadString(device, device->Descriptor.Product, buffer, 0x100);
			if (result == OK)
				printf("USBD:  -Product:       %s.\n", buffer);
		}
	}
	if (device->Descriptor.Manufacturer != 0)
	{
		if (buffer == NULL)
			buffer = MemoryAllocate(0x100);
		if (buffer != NULL)
		{
			result = UsbReadString(device, device->Descriptor.Manufacturer, buffer, 0x100);
			if (result == OK)
				printf("USBD:  -Manufacturer:  %s.\n", buffer);
		}
	}
	if (device->Descriptor.SerialNumber != 0)
	{
		if (buffer == NULL)
			buffer = MemoryAllocate(0x100);
		if (buffer != NULL)
		{
			result = UsbReadString(device, device->Descriptor.SerialNumber, buffer, 0x100);
			if (result == OK)
				printf("USBD:  -SerialNumber:  %s.\n", buffer);
		}
	}

	if (buffer != NULL)
	{
		MemoryDeallocate(buffer);
		buffer = NULL;
	}

	printf("USBD:  -VID:PID:       %x:%x v%d.%x\n", device->Descriptor.VendorId, device->Descriptor.ProductId, device->Descriptor.Version >> 8, device->Descriptor.Version & 0xff);

	// We only support devices with 1 configuration for now.
	if ((result = UsbConfigure(device, 0)) != OK)
	{
		printf("USBD: Failed to configure device %#x.\n", address);
		return OK;
	}

	if (device->Configuration.StringIndex != 0)
	{
		if (buffer == NULL)
			buffer = MemoryAllocate(0x100);
		if (buffer != NULL)
		{
			result = UsbReadString(device, device->Configuration.StringIndex, buffer, 0x100);
			if (result == OK)
				printf("USBD:  -Configuration: %s.\n", buffer);
		}
	}

	if (buffer != NULL)
	{
		MemoryDeallocate(buffer);
		buffer = NULL;
	}

	if (device->Interfaces[0].Class < InterfaceClassAttachCount &&
		InterfaceClassAttach[device->Interfaces[0].Class] != NULL)
	{
		if ((result = InterfaceClassAttach[device->Interfaces[0].Class](device, 0)) != OK)
		{
			printf("USBD: Could not start the driver for %s.\n", UsbGetDescription(device));
		}
	}

	return OK;
}

void UsbDeallocateDevice(struct UsbDevice *device)
{
	printf("USBD: Deallocating device %d: %s.\n", device->Number, UsbGetDescription(device));

	if (device->DeviceDetached != NULL)
		device->DeviceDetached(device);
	if (device->DeviceDeallocate != NULL)
		device->DeviceDeallocate(device);

	if (device->Parent != NULL && device->Parent->DeviceChildDetached != NULL)
		device->Parent->DeviceChildDetached(device->Parent, device);

	if (device->Status == Addressed || device->Status == Configured)
		if (device->Number > 0 && device->Number <= MaximumDevices && Devices[device->Number - 1] == device)
			Devices[device->Number - 1] = NULL;

	if (device->FullConfiguration != NULL)
		MemoryDeallocate((void *)device->FullConfiguration);

	MemoryDeallocate(device);
}

Result UsbGetString(struct UsbDevice *device, uint8_t stringIndex, uint16_t langId, void *buffer, uint32_t length)
{
	Result result;

	// Apparently this tends to fail a lot.
	for (uint32_t i = 0; i < 3; i++)
	{
		result = UsbGetDescriptor(device, String, stringIndex, langId, buffer, length, length, 0);

		if (result == OK)
			break;
	}

	return result;
}