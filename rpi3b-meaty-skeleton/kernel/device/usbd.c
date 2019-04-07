#include <kernel/systimer.h>
#include <kernel/types.h>

#include <device/keyboard.h>
#include <device/mouse.h>
#include <device/hcd.h>
#include <device/hid.h>
#include <device/usb-mem.h>
#include <plibc/stdio.h>
#include <device/usbd.h>
#include <device/hub.h>

/** The default timeout in ms of control transfers. */
#define ControlMessageTimeout 10
/** The maximum number of devices that can be connected. */
#define MaximumDevices 32
#define InterfaceClassAttachCount 16
struct UsbDevice *Devices[MaximumDevices];
Result (*InterfaceClassAttach[InterfaceClassAttachCount])(struct UsbDevice *device, uint32_t interfaceNumber);
static int TreeLevelInUse[20] = {0};

struct UsbDevice *UsbGetRootHub(void)
{
	return Devices[0]; // Return NULL as no valid rootHub
}

void UsbShowTree(struct UsbDevice *root, const int level, const char tee)
{
	// int maxPacket;
	for (int i = 0; i < level - 1; i++)
	{
		if (TreeLevelInUse[i] == 0)
		{
			printf("   ");
		}
		else
		{
			printf(" %c ", '\xB3'); // Draw level lines if in use
		}
	}
	int maxPacket = SizeFromNumber(root->Descriptor.MaxPacketSize0);
	struct HubDevice *hubDev = (struct HubDevice *)root->DriverData;

	printf(" %c-%s id: %d port: %d speed: %s packetsize: %d \n", tee, UsbGetDescription(root), root->Number, root->PortNumber, SpeedToChar(root->Speed), maxPacket);
	// printf(" %c --%s id: %d port: %d \n", tee, UsbGetDescription(root), root->PortNumber, root->Parent->PortNumber); // Print this entry
	if (root->DriverData->DeviceDriver == DeviceDriverHub)
	{

		int lastChild = hubDev->MaxChildren;
		for (int i = 0; i < lastChild; i++)
		{						   // For each child of hub
			char nodetee = '\xC0'; // Preset nodetee to end node ... "L"
			for (int j = i; j < lastChild - 1; j++)
			{ // Check if any following child node is valid
				if (hubDev->Children[j + 1])
				{							   // We found a following node in use
					TreeLevelInUse[level] = 1; // Set tree level in use flag
					nodetee = (char)0xc3;	  // Change the node character to tee looks like this "├"
					break;					   // Exit loop j
				};
			}
			if (hubDev->Children[i])
			{														  // If child valid
				UsbShowTree(hubDev->Children[i], level + 1, nodetee); // Iterate into child but level+1 down of coarse
			}
			TreeLevelInUse[level] = 0; // Clear level in use flag
		}
	}
	// else
	// {
	// 	printf("Descriptor is not hub. :%d \n", root->Descriptor.DescriptorType);
	// }
}

Result UsbControlMessage(struct UsbDevice *device,
						 struct UsbPipeAddress pipe, void *input_buffer, uint32_t bufferLength,
						 struct UsbDeviceRequest *request, uint32_t timeout)
{
	Result result;

	uint32_t *buffer = (uint32_t *)input_buffer;
	// printf("USBD_PRAKASH: Input buffer. address: %x buffer:%x \n", input_buffer, buffer);
	if (((uint32_t)buffer & 0x3) != 0)
	{
		printf("USBD: Warning message buffer not word aligned. address: %x \n", buffer);
	}

	result = HcdSumbitControlMessage(device, pipe, buffer, bufferLength, request);

	if (result != OK)
	{
		printf("USBD: Failed to send message to %s: %d.\n", UsbGetDescription(device), result);
		return result;
	}
	// else
	// {
	// 	printf("USBD_PRAKASH: HcdSubmit control message ok.\n");
	// }

	while (timeout-- > 0 && (device->Error & Processing))
	{

		uint64_t timer_tick = timer_getTickCount64();
		while (timer_getTickCount64() < (timer_tick + 10000))
			;
		// printf("Wating 10000 micros seconds completed .\n");
	}
	// printf("USBD_PRAKASH: Wating complete.\n");
	if ((device->Error & Processing))
	{
		printf("USBD: Message to %s timeout reached.\n", UsbGetDescription(device));
		return ErrorTimeout;
	}

	if (device->Error & ~Processing)
	{
		printf("USBD:device->Error & ~Processing.\n");
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
		else
		{
			printf("USBD_PRAKASH: device->Parent->DeviceCheckConnection: %x.\n", device->Parent->DeviceCheckConnection);
		}
		result = ErrorDevice;
	}

	// printf("USBD_PRAKASH: Control message  exiting at end. result: %d \n", result);
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
		printf("USBD: Illegal attempt to configure device %s in state %x.\n", UsbGetDescription(device), device->Status);
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
		printf("USBD: Failed to get descriptor %x:%x for device %s. Result %x.\n", type, index, UsbGetDescription(device), result);
		return result;
	}

	if (device->LastTransfer < minimumLength)
	{
		printf("USBD: Unexpectedly short descriptor (%d/%d) %x:%x for device %s. Result %x.\n", device->LastTransfer, minimumLength, type, index, UsbGetDescription(device), result);
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
		return "USB Fake Root Hub\0";

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
		printf("USBD: Illegal attempt to configure device %s in state %x.\n", UsbGetDescription(device), device->Status);
		return ErrorDevice;
	}

	if ((result = UsbGetDescriptor(device, Configuration, configuration, 0, (void *)&device->Configuration, sizeof(device->Configuration), sizeof(device->Configuration), 0)) != OK)
	{
		printf("USBD: Failed to retrieve configuration descriptor %x for device %s.\n", configuration, UsbGetDescription(device));
		return result;
	}

	if ((fullDescriptor = MemoryAllocate(device->Configuration.TotalLength)) == NULL)
	{
		printf("USBD: Failed to allocate space for descriptor.\n");
		return ErrorMemory;
	}
	if ((result = UsbGetDescriptor(device, Configuration, configuration, 0, fullDescriptor, device->Configuration.TotalLength, device->Configuration.TotalLength, 0)) != OK)
	{
		printf("USBD: Failed to retrieve full configuration descriptor %x for device %s.\n", configuration, UsbGetDescription(device));
		goto deallocate;
	}

	device->ConfigurationIndex = configuration;
	configuration = device->Configuration.ConfigurationValue;
	printf("USBD: Configuration device->Configuration.TotalLength: %d \n", device->Configuration.TotalLength);
	printf("USBD_INTERFACE_COUNT: : %d \n", device->Configuration.InterfaceCount);
	header = fullDescriptor;
	lastInterface = MaxInterfacesPerDevice;
	lastEndpoint = MaxEndpointsPerDevice;
	isAlternate = false;

	// printf("USBD_PRAKASH: Entering for loop: ---------------------************************\n");
	for (header = (struct UsbDescriptorHeader *)((uint8_t *)header + header->DescriptorLength);
		 (uint32_t)header - (uint32_t)fullDescriptor < device->Configuration.TotalLength;
		 header = (struct UsbDescriptorHeader *)((uint8_t *)header + header->DescriptorLength))
	{
		// printf("USBD_PRAKASH: in for loop: \n");
		switch (header->DescriptorType)
		{
		case Interface:
			// printf("USBD_PRAKASH: Interface Descriptor type: \n");
			interface = (struct UsbInterfaceDescriptor *)header;
			// printf("USBD_PRAKASH_ENDPOINT_COUNT: %d: \n", interface->EndpointCount);
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
			// printf("USBD_PRAKASH: End point Descriptor type: \n");
			if (isAlternate)
			{
				break;
				printf("USBD_PRAKASH: End point Descriptor type Is alternate break: \n");
			}

			if (lastInterface == MaxInterfacesPerDevice || lastEndpoint >= device->Interfaces[lastInterface].EndpointCount)
			{
				printf("USBD: Unexpected endpoint descriptor in %s.Interface%d.\n", UsbGetDescription(device), lastInterface + 1);
				break;
			}
			endpoint = (struct UsbEndpointDescriptor *)header;
			MemoryCopy((void *)&device->Endpoints[lastInterface][lastEndpoint++], (void *)endpoint, sizeof(struct UsbEndpointDescriptor));
			// printf("USBD_PRAKASH: End point Descriptor type mmcopied: \n");
			break;
		default:
			// printf("USBD_PRAKASH: DEFAULT Descriptor type: \n");
			if (header->DescriptorLength == 0)
			{
				printf("USBD_PRAKASH: DEFAULT Descriptor type DEsc length 0. go to: \n");
				goto headerLoopBreak;
			}

			break;
		}

		// printf("USBD: Descriptor %d length %d, interface %d.\n", header->DescriptorType, header->DescriptorLength, lastInterface);
	}
	// printf("USBD_PRAKASH: Out of for loop: ---------------------************************\n");
headerLoopBreak:

	if ((result = UsbSetConfiguration(device, configuration)) != OK)
	{
		printf("USBD_PRAK: UsbSetConfiguration not ok: \n");
		goto deallocate;
	}
	printf("USBD: %s configuration %d. Class %d, subclass %d.\n", UsbGetDescription(device), configuration, device->Interfaces[0].Class, device->Interfaces[0].SubClass);

	device->FullConfiguration = fullDescriptor;
	printf("USBD_PRAK: returning usb configure success: \n");
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

Result UsbSetInterface(struct UsbDevice *device, uint8_t interface)
{
	Result result;

	if (device->Status != Default)
	{
		printf("USBD: Illegal attempt to configure device %s in state %x.\n", UsbGetDescription(device), device->Status);
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
				 .Request = SetInterface,
				 .Type = 0,
				 .Value = interface,
			 },
			 ControlMessageTimeout)) != OK)
		return result;

	MicroDelay(10000);

	printf("USBD_PRAKASH: Interface  set successful.\n");
	return OK;
}

Result UsbSetAddress(struct UsbDevice *device, uint8_t address)
{
	Result result;

	if (device->Status != Default)
	{
		printf("USBD: Illegal attempt to configure device %s in state %x.\n", UsbGetDescription(device), device->Status);
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

	MicroDelay(10000);

	device->Number = address;
	device->Status = Addressed;

	printf("USBD_PRAKASH: Address set successful.\n");
	return OK;
}

Result UsbInitialise()
{
	Result result = OK;

	// Init usb mem allocation
	PlatformLoad();
	HubLoad();
	HidLoad();
	KbdLoad();
	MouseLoad();

	if (sizeof(struct UsbDeviceRequest) != 0x8)
	{
		printf("USBD: Incorrectly compiled driver. UsbDeviceRequest: %x (0x8).\n",
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

	// UsbCheckForChange();
	UsbShowTree(UsbGetRootHub(), 1, '+');
	// while (1)
	// {
	// 	UsbCheckForChange();
	// 	MicroDelay(30000);
	// }
	return OK;
}

Result UsbAttachRootHub()
{
	Result result;
	struct UsbDevice *rootHub;
	printf("USBD_PRAKASH: Scanning for devices. \n");
	if (Devices[0] != NULL)
	{
		UsbDeallocateDevice(Devices[0]);
		printf("USBD: de allocated earlier device. \n");
	}
	if ((result = UsbAllocateDevice(&rootHub)) != OK)
	{
		printf("USBD_PRAKASH: Could not allocated root hub. \n");
		return result;
	}

	Devices[0]->Status = Powered;

	printf("USBD_PRAKASH: Calling USB Attach Device. \n");
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

	printf("USBD: Allocating new device, address %x.\n", (*device)->Number);

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

	printf("USBD_CONF_COUNT: %d \n", device->Descriptor.ConfigurationCount);

	device->Status = Default;

	if (device->Parent != NULL)
	{
		printf("USBD_PRAK: Found real device.\n");
		if (device->Parent->DeviceChildReset != NULL)
		{
			// DeviceChildReset -> HubChildReset
			// Reset the port for what will be the second time.
			if ((result = device->Parent->DeviceChildReset(device->Parent, device)) != OK)
			{
				printf("USBD: Failed to reset port again for new device %s.\n", UsbGetDescription(device));
				device->Number = address;
				return result;
			}
			else
			{
				printf("USBD_PRAK: DeviceChildReset Child reset successful.\n");
			}
		}
		else
		{
			printf("USBD_PRAK: DeviceChildReset is not set.\n");
		}
	}
	else
	{
		printf("USBD_PRAK: Found Fake root device.\n");
	}

	if ((result = UsbSetAddress(device, address)) != OK)
	{
		printf("USBD: Failed to assign address to %x.\n", address);
		device->Number = address;
		return result;
	}

	device->Number = address;

	MicroDelay(10000);

	if ((result = UsbReadDeviceDescriptor(device)) != OK)
	{
		printf("USBD: Failed to reread device descriptor for %x.\n", address);
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
		printf("USBD: Failed to configure device %x.\n", address);
		return OK;
	}

	if (device->Configuration.StringIndex != 0)
	{
		if (buffer == NULL)
		{
			buffer = MemoryAllocate(0x100);
			printf("USBD:  Allocated buffer: %s.\n");
		}
		if (buffer != NULL)
		{
			result = UsbReadString(device, device->Configuration.StringIndex, buffer, 0x100);
			if (result == OK)
			{
				printf("USBD:  -Configuration: %s.\n", buffer);
			}
			else
			{
				printf("USBD:  -Configuration: Cannot read string.\n");
			}
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
	else
	{
		printf("USBD_PRAKASH:  No class found.\n");
	}
	printf("USBD_PRAKASH:  Successfully allocated root hub.\n");
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

void UsbCheckForChange()
{

	if (Devices[0] != NULL && Devices[0]->DeviceCheckForChange != NULL)
	{
		// printf("Calling check for change. \n");
		Devices[0]->DeviceCheckForChange(Devices[0]);
	}
	else
	{
		// printf("check for change callback not assigned. \n");
	}
}