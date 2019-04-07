#include <device/hub.h>
#include <device/hcd.h>
#include <device/usbd.h>
#include <device/usb-mem.h>
#include <stdint.h>
#include <kernel/types.h>
#include <plibc/stdio.h>
#include <kernel/systimer.h>

#define InterfaceClassAttachCount 16
#define ControlMessageTimeout 10
extern Result (*InterfaceClassAttach[InterfaceClassAttachCount])(struct UsbDevice *device, uint32_t interfaceNumber);

void HubLoad()
{
    printf("CSUD: Hub driver version 0.1\n");
    InterfaceClassAttach[InterfaceClassHub] = HubAttach;
}

Result HubReadDescriptor(struct UsbDevice *device)
{
    struct UsbDescriptorHeader header;
    Result result;

    if ((result = UsbGetDescriptor(device, Hub, 0, 0, &header, sizeof(header), sizeof(header), 0x20)) != OK)
    {
        printf("HUB: Failed to read hub descriptor for %s.\n", UsbGetDescription(device));
        return result;
    }

    if (((struct HubDevice *)device->DriverData)->Descriptor == NULL &&
        (((struct HubDevice *)device->DriverData)->Descriptor = MemoryAllocate(header.DescriptorLength)) == NULL)
    {
        printf("HUB: Not enough memory to read hub descriptor for %s.\n", UsbGetDescription(device));
        return ErrorMemory;
    }
    if ((result = UsbGetDescriptor(device, Hub, 0, 0, ((struct HubDevice *)device->DriverData)->Descriptor, header.DescriptorLength, header.DescriptorLength, 0x20)) != OK)
    {
        printf("HUB: Failed to read hub descriptor for %s.\n", UsbGetDescription(device));
        return result;
    }

    return OK;
}

Result HubGetStatus(struct UsbDevice *device)
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
             &((struct HubDevice *)device->DriverData)->Status,
             sizeof(struct HubFullStatus),
             &(struct UsbDeviceRequest){
                 .Request = GetStatus,
                 .Type = 0xa0,
                 .Length = sizeof(struct HubFullStatus),
             },
             ControlMessageTimeout)) != OK)
        return result;
    if (device->LastTransfer < sizeof(struct HubFullStatus))
    {
        printf("HUB: Failed to read hub status for %s.\n", UsbGetDescription(device));
        return ErrorDevice;
    }
    return OK;
}

Result HubPortGetStatus(struct UsbDevice *device, uint8_t port)
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
             &((struct HubDevice *)device->DriverData)->PortStatus[port],
             sizeof(struct HubPortFullStatus),
             &(struct UsbDeviceRequest){
                 .Request = GetStatus,
                 .Type = 0xa3,
                 .Index = port + 1,
                 .Length = sizeof(struct HubPortFullStatus),
             },
             ControlMessageTimeout)) != OK)
        return result;
    if (device->LastTransfer < sizeof(struct HubPortFullStatus))
    {
        printf("HUB: Failed to read hub port status for %s.Port%d.\n", UsbGetDescription(device), port + 1);
        return ErrorDevice;
    }
    return OK;
}

Result HubChangeFeature(struct UsbDevice *device, enum HubFeature feature, bool set)
{
    Result result;

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
                 .Request = set ? SetFeature : ClearFeature,
                 .Type = 0x20,
                 .Value = (uint8_t)feature,
             },
             ControlMessageTimeout)) != OK)
        return result;

    return OK;
}

Result HubChangePortFeature(struct UsbDevice *device, enum HubPortFeature feature, uint8_t port, bool set)
{
    Result result;

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
                 .Request = set ? SetFeature : ClearFeature,
                 .Type = 0x23,
                 .Value = (uint16_t)feature,
                 .Index = port + 1,
             },
             ControlMessageTimeout)) != OK)
        return result;

    return OK;
}

Result HubPowerOn(struct UsbDevice *device)
{
    struct HubDevice *data;
    struct HubDescriptor *hubDescriptor;

    data = (struct HubDevice *)device->DriverData;
    hubDescriptor = data->Descriptor;
    printf("HUB: Powering on hub %s.\n", UsbGetDescription(device));
    printf("HUB: Powering on hub data->MaxChildren %d.\n", data->MaxChildren);
    for (uint32_t i = 0; i < data->MaxChildren; i++)
    {
        if (HubChangePortFeature(device, FeaturePower, i, true) != OK)
        {
            printf("HUB: Could not power %s.Port %d.\n", UsbGetDescription(device), i + 1);
        }
        else
        {
            printf("HUB: Success powered on %s.Port %d.\n", UsbGetDescription(device), i + 1);
        }
    }

    MicroDelay(hubDescriptor->PowerGoodDelay * 2000);

    return OK;
}

Result HubPortReset(struct UsbDevice *device, uint8_t port)
{
    Result result;
    struct HubDevice *data;
    struct HubPortFullStatus *portStatus;
    uint32_t retry, timeout;

    data = (struct HubDevice *)device->DriverData;
    portStatus = &data->PortStatus[port];

    printf("HUB: Hub reset %s.Port%d.\n", UsbGetDescription(device), port + 1);
    for (retry = 0; retry < 3; retry++)
    {
        if ((result = HubChangePortFeature(device, FeatureReset, port, true)) != OK)
        {
            printf("HUB: Failed to reset %s.Port%d.\n", UsbGetDescription(device), port + 1);
            return result;
        }
        timeout = 0;
        do
        {
            MicroDelay(20000);
            if ((result = HubPortGetStatus(device, port)) != OK)
            {
                printf("HUB: Hub failed to get status (4) for %s.Port%d.\n", UsbGetDescription(device), port + 1);
                return result;
            }
            timeout++;
        } while (!portStatus->Change.ResetChanged && !portStatus->Status.Enabled && timeout < 10);

        if (timeout == 10)
            continue;

        // printf("HUB: %s.Port%d Status %x:%x.\n", UsbGetDescription(device), port + 1, *(uint16_t *)&portStatus->Status, *(uint16_t *)&portStatus->Change);

        if (portStatus->Change.ConnectedChanged || !portStatus->Status.Connected)
            return ErrorDevice;

        if (portStatus->Status.Enabled)
            break;
    }

    if (retry == 3)
    {
        printf("HUB: Cannot enable %s.Port%d. Please verify the hardware is working.\n", UsbGetDescription(device), port + 1);
        return ErrorDevice;
    }

    if ((result = HubChangePortFeature(device, FeatureResetChange, port, false)) != OK)
    {
        printf("HUB: Failed to clear reset on %s.Port%d.\n", UsbGetDescription(device), port + 1);
    }
    printf("HUB_PRAK: Hub reset %s.Port %d Complete success.\n", UsbGetDescription(device), port + 1);
    return OK;
}

Result HubPortConnectionChanged(struct UsbDevice *device, uint8_t port)
{
    printf("\n------------------_Connection changed-----------------\n");
    Result result;
    struct HubDevice *data;
    struct HubPortFullStatus *portStatus;

    data = (struct HubDevice *)device->DriverData;
    portStatus = &data->PortStatus[port];

    if ((result = HubPortGetStatus(device, port)) != OK)
    {
        printf("HUB: Hub failed to get status (2) for %s.Port%d.\n", UsbGetDescription(device), port + 1);
        return result;
    }
    // printf("HUB: %s.Port%d Status %x:%x.\n", UsbGetDescription(device), port + 1, *(uint16_t *)&portStatus->Status, *(uint16_t *)&portStatus->Change);

    if ((result = HubChangePortFeature(device, FeatureConnectionChange, port, false)) != OK)
    {
        printf("HUB: Failed to clear change on %s.Port%d.\n", UsbGetDescription(device), port + 1);
    }

    if ((!portStatus->Status.Connected && !portStatus->Status.Enabled) || data->Children[port] != NULL)
    {
        printf("HUB: Disconnected %s.Port%d - %s.\n", UsbGetDescription(device), port + 1, UsbGetDescription(data->Children[port]));
        UsbDeallocateDevice(data->Children[port]);
        data->Children[port] = NULL;
        if (!portStatus->Status.Connected)
            return OK;
    }

    if ((result = HubPortReset(device, port)) != OK)
    {
        printf("HUB: Could not reset %s.Port%d for new device.\n", UsbGetDescription(device), port + 1);
        return result;
    }

    if ((result = UsbAllocateDevice(&data->Children[port])) != OK)
    {
        printf("HUB: Could not allocate a new device entry for %s.Port%d.\n", UsbGetDescription(device), port + 1);
        return result;
    }

    if ((result = HubPortGetStatus(device, port)) != OK)
    {
        printf("HUB: Hub failed to get status (3) for %s.Port%d.\n", UsbGetDescription(device), port + 1);
        return result;
    }

    // printf("HUB: %s.Port%d Status %x:%x.\n", UsbGetDescription(device), port + 1, *(uint16_t *)&portStatus->Status, *(uint16_t *)&portStatus->Change);

    if (portStatus->Status.HighSpeedAttatched)
        data->Children[port]->Speed = High;
    else if (portStatus->Status.LowSpeedAttatched)
        data->Children[port]->Speed = Low;
    else
        data->Children[port]->Speed = Full;
    data->Children[port]->Parent = device;
    data->Children[port]->PortNumber = port;
    if ((result = UsbAttachDevice(data->Children[port])) != OK)
    {
        printf("HUB: Could not connect to new device in %s.Port%d. Disabling.\n", UsbGetDescription(device), port + 1);
        UsbDeallocateDevice(data->Children[port]);
        data->Children[port] = NULL;
        if (HubChangePortFeature(device, FeatureEnable, port, false) != OK)
        {
            printf("HUB: Failed to disable %s.Port%d.\n", UsbGetDescription(device), port + 1);
        }
        return result;
    }
    return OK;
}

void HubDetached(struct UsbDevice *device)
{
    struct HubDevice *data;

    printf("HUB: %s detached.\n", UsbGetDescription(device));
    if (device->DriverData != NULL)
    {
        data = (struct HubDevice *)device->DriverData;

        for (uint32_t i = 0; i < data->MaxChildren; i++)
        {
            if (data->Children[i] != NULL &&
                data->Children[i]->DeviceDetached != NULL)
                data->Children[i]->DeviceDetached(data->Children[i]);
        }
    }
}

void HubDeallocate(struct UsbDevice *device)
{
    struct HubDevice *data;

    printf("HUB: %s deallocate.\n", UsbGetDescription(device));
    if (device->DriverData != NULL)
    {
        data = (struct HubDevice *)device->DriverData;

        for (uint32_t i = 0; i < data->MaxChildren; i++)
        {
            if (data->Children[i] != NULL)
            {
                UsbDeallocateDevice(data->Children[i]);
                data->Children[i] = NULL;
            }
        }

        if (data->Descriptor != NULL)
            MemoryDeallocate(data->Descriptor);
        MemoryDeallocate((void *)device->DriverData);
    }
    device->DeviceDeallocate = NULL;
    device->DeviceDetached = NULL;
    device->DeviceCheckForChange = NULL;
    device->DeviceChildDetached = NULL;
    device->DeviceChildReset = NULL;
    device->DeviceCheckConnection = NULL;
}

void HubCheckForChange(struct UsbDevice *device)
{
    struct HubDevice *data;

    data = (struct HubDevice *)device->DriverData;
    for (uint32_t i = 0; i < data->MaxChildren; i++)
    {
        if (HubCheckConnection(device, i) != OK)
        {
            printf("HUB_PRAKASH: In hub check connection not ok for child : %d. \n", i);
            continue;
        }

        if (data->Children[i] != NULL &&
            data->Children[i]->DeviceCheckForChange != NULL)
            data->Children[i]->DeviceCheckForChange(data->Children[i]);
    }
}

void HubChildDetached(struct UsbDevice *device, struct UsbDevice *child)
{
    struct HubDevice *data;

    data = (struct HubDevice *)device->DriverData;
    //  if (child->Parent == device && child->PortNumber >= 0 && child->PortNumber < data->MaxChildren &&
    if (child->Parent == device && child->PortNumber < data->MaxChildren &&
        data->Children[child->PortNumber] == child)
        data->Children[child->PortNumber] = NULL;
}

Result HubChildReset(struct UsbDevice *device, struct UsbDevice *child)
{
    struct HubDevice *data;

    data = (struct HubDevice *)device->DriverData;

    //Before signed unsigned change for PortNumber
    // if (child->Parent == device && child->PortNumber >= 0 && child->PortNumber < data->MaxChildren &&
    if (child->Parent == device && child->PortNumber < data->MaxChildren &&
        data->Children[child->PortNumber] == child)
    {
        return HubPortReset(device, child->PortNumber);
    }
    else
    {
        return ErrorDevice;
    }
}

Result HubCheckConnectionDevice(struct UsbDevice *device, struct UsbDevice *child)
{
    struct HubDevice *data;
    Result result;

    data = (struct HubDevice *)device->DriverData;

    // if (child->Parent == device && child->PortNumber >= 0 && child->PortNumber < data->MaxChildren &&
    if (child->Parent == device && child->PortNumber < data->MaxChildren &&
        data->Children[child->PortNumber] == child)
    {
        if ((result = HubCheckConnection(device, child->PortNumber)) != OK)
            return result;
        return data->Children[child->PortNumber] == child ? OK : ErrorDisconnected;
    }
    else
        return ErrorArgument;
}

Result HubCheckConnection(struct UsbDevice *device, uint8_t port)
{
    Result result;
    struct HubPortFullStatus *portStatus;
    struct HubDevice *data;

    data = (struct HubDevice *)device->DriverData;

    if ((result = HubPortGetStatus(device, port)) != OK)
    {
        if (result != ErrorDisconnected)
            printf("HUB: Failed to get hub port status (1) for %s.Port%d.\n", UsbGetDescription(device), port + 1);
        return result;
    }
    // else
    // {
    //     printf("HUB: PORT STATUS OK for :%d.\n", port);
    // }

    portStatus = &data->PortStatus[port];

    if (portStatus->Change.ConnectedChanged)
    {
        printf("HUB_PRAKASH: Hub port connection changed for :%d.\n", port);
        HubPortConnectionChanged(device, port);
    }
    // else
    // {
    //     printf("HUB_PRAKASH: Hub port connection NOT changed for :%d.\n", port);
    // }

    if (portStatus->Change.EnabledChanged)
    {
        if (HubChangePortFeature(device, FeatureEnableChange, port, false) != OK)
        {
            printf("HUB: Failed to clear enable change %s.Port%d.\n", UsbGetDescription(device), port + 1);
        }

        // This may indicate EM interference.
        if (!portStatus->Status.Enabled && portStatus->Status.Connected && data->Children[port] != NULL)
        {
            printf("HUB: %s.Port%d has been disabled, but is connected. This can be cause by interference. Reenabling!\n", UsbGetDescription(device), port + 1);
            HubPortConnectionChanged(device, port);
        }
    }
    if (portStatus->Status.Suspended)
    {
        if (HubChangePortFeature(device, FeatureSuspend, port, false) != OK)
        {
            printf("HUB: Failed to clear suspended port - %s.Port%d.\n", UsbGetDescription(device), port + 1);
        }
    }
    if (portStatus->Change.OverCurrentChanged)
    {
        if (HubChangePortFeature(device, FeatureOverCurrentChange, port, false) != OK)
        {
            printf("HUB: Failed to clear over current port - %s.Port%d.\n", UsbGetDescription(device), port + 1);
        }
        HubPowerOn(device);
    }
    if (portStatus->Change.ResetChanged)
    {
        if (HubChangePortFeature(device, FeatureResetChange, port, false) != OK)
        {
            printf("HUB: Failed to clear reset port - %s.Port%d.\n", UsbGetDescription(device), port + 1);
        }
    }

    // printf("HUB_PRAKASH: returning hub check connection :%d.\n", port);
    return OK;
}

Result HubAttach(struct UsbDevice *device, uint32_t interfaceNumber)
{
    Result result;
    struct HubDevice *data;
    struct HubDescriptor *hubDescriptor;
    struct HubFullStatus *status;

    if (device->Interfaces[interfaceNumber].EndpointCount != 1)
    {
        printf("HUB: Cannot enumerate hub with multiple endpoints: %d.\n", device->Interfaces[interfaceNumber].EndpointCount);
        return ErrorIncompatible;
    }
    if (device->Endpoints[interfaceNumber][0].EndpointAddress.Direction == Out)
    {
        printf("HUB: Cannot enumerate hub with only one output endpoint.\n");
        return ErrorIncompatible;
    }
    if (device->Endpoints[interfaceNumber][0].Attributes.Type != Interrupt)
    {
        printf("HUB: Cannot enumerate hub without interrupt endpoint.\n");
        return ErrorIncompatible;
    }
    printf("HUB_PRAKASH: Attaching HUB. \n");
    device->DeviceDeallocate = HubDeallocate;
    device->DeviceDetached = HubDetached;
    device->DeviceCheckForChange = HubCheckForChange;
    device->DeviceChildDetached = HubChildDetached;
    device->DeviceChildReset = HubChildReset;
    device->DeviceCheckConnection = HubCheckConnectionDevice;
    if ((device->DriverData = MemoryAllocate(sizeof(struct HubDevice))) == NULL)
    {
        printf("HUB: Cannot allocate hub data. Out of memory.\n");
        return ErrorMemory;
    }
    data = (struct HubDevice *)device->DriverData;
    device->DriverData->DataSize = sizeof(struct HubDevice);
    device->DriverData->DeviceDriver = DeviceDriverHub;
    for (uint32_t i = 0; i < MaxChildrenPerDevice; i++)
        data->Children[i] = NULL;

    if ((result = HubReadDescriptor(device)) != OK)
        return result;

    hubDescriptor = data->Descriptor;
    printf("HUB_PRAKASH: HUB port count: %d \n", hubDescriptor->PortCount);

    if (hubDescriptor->PortCount > MaxChildrenPerDevice)
    {
        printf("HUB: Hub %s is too big for this driver to handle. Only the first %d ports will be used. Change MaxChildrenPerDevice in usbd/device.h.\n", UsbGetDescription(device), MaxChildrenPerDevice);
        data->MaxChildren = MaxChildrenPerDevice;
    }
    else
        data->MaxChildren = hubDescriptor->PortCount;

    switch (hubDescriptor->Attributes.PowerSwitchingMode)
    {
    case Global:
        printf("HUB: Hub power: Global.\n");
        break;
    case Individual:
        printf("HUB: Hub power: Individual.\n");
        break;
    default:
        printf("HUB: Unknown hub power type %d on %s. Driver incompatible.\n", hubDescriptor->Attributes.PowerSwitchingMode, UsbGetDescription(device));
        HubDeallocate(device);
        return ErrorIncompatible;
    }

    if (hubDescriptor->Attributes.Compound)
        printf("HUB: Hub nature: Compound.\n");
    else
        printf("HUB: Hub nature: Standalone.\n");

    switch (hubDescriptor->Attributes.OverCurrentProtection)
    {
    case Global:
        printf("HUB: Hub over current protection: Global.\n");
        break;
    case Individual:
        printf("HUB: Hub over current protection: Individual.\n");
        break;
    default:
        printf("HUB: Unknown hub over current type %d on %s. Driver incompatible.\n", hubDescriptor->Attributes.OverCurrentProtection, UsbGetDescription(device));
        HubDeallocate(device);
        return ErrorIncompatible;
    }

    printf("HUB: Hub power to good: %dms.\n", hubDescriptor->PowerGoodDelay * 2);
    printf("HUB: Hub current required: %dmA.\n", hubDescriptor->MaximumHubPower * 2);
    printf("HUB: Hub ports: %d.\n", hubDescriptor->PortCount);

    printf("HUB_PRAKASH: HUB data->MaxChildren: %d \n", data->MaxChildren);
    for (uint32_t i = 0; i < data->MaxChildren; i++)
    {
        if (hubDescriptor->Data[(i + 1) >> 3] & 1 << ((i + 1) & 0x7))
            printf("HUB: Hub port %d is not removable.\n", i + 1);
        else
            printf("HUB: Hub port %d is removable.\n", i + 1);
    }

    if ((result = HubGetStatus(device)) != OK)
    {
        printf("HUB: Failed to get hub status for %s.\n", UsbGetDescription(device));
        return result;
    }
    status = &data->Status;

    if (!status->Status.LocalPower)
        printf("USB Hub power: Good.\n");
    else
        printf("HUB: Hub power: Lost.\n");
    if (!status->Status.OverCurrent)
        printf("USB Hub over current condition: No.\n");
    else
        printf("HUB: Hub over current condition: Yes.\n");

    printf("HUB: Hub powering on.\n");
    if ((result = HubPowerOn(device)) != OK)
    {
        printf("HUB: Hub failed to power on.\n");
        HubDeallocate(device);
        return result;
    }

    if ((result = HubGetStatus(device)) != OK)
    {
        printf("HUB: Failed to get hub status for %s.\n", UsbGetDescription(device));
        HubDeallocate(device);
        return result;
    }

    if (!status->Status.LocalPower)
        printf("USB Hub power: Good.\n");
    else
        printf("HUB: Hub power: Lost.\n");
    if (!status->Status.OverCurrent)
        printf("USB Hub over current condition: No.\n");
    else
        printf("HUB: Hub over current condition: Yes.\n");

    // printf("HUB: %s status %x:%x.\n", UsbGetDescription(device), *(uint16_t *)&status->Status, *(uint16_t *)&status->Change);

    for (uint8_t port = 0; port < data->MaxChildren; port++)
    {
        HubCheckConnection(device, port);
    }

    // printf("HUB_PRAKASH: Returning from HUB Attach fucntion. \n");

    return OK;
}