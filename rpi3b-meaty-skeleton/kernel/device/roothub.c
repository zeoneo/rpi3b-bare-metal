#include <wchar.h>
#include <device/roothub.h>
#include <device/hub.h>
#include <device/hcd.h>
#include <device/usbd.h>
#include <device/usb-mem.h>
#include <stdint.h>
#include <kernel/types.h>
#include <kernel/systimer.h>
#include <plibc/stdio.h>
#include <stdlib.h>

extern volatile struct PowerReg *Power;
struct UsbDeviceDescriptor DeviceDescriptor = {
    .DescriptorLength = 0x12,
    .DescriptorType = Device,
    .UsbVersion = 0x0200,
    .Class = DeviceClassHub,
    .SubClass = 0,
    .Protocol = 0,
    .MaxPacketSize0 = 8,
    .VendorId = 0,
    .ProductId = 0,
    .Version = 0x0100,
    .Manufacturer = 0,
    .Product = 1,
    .SerialNumber = 0,
    .ConfigurationCount = 1,
};

struct
{
    struct UsbConfigurationDescriptor Configuration;
    struct UsbInterfaceDescriptor Interface;
    struct UsbEndpointDescriptor Endpoint;
} __attribute__((__packed__)) ConfigurationDescriptor = {
    .Configuration = {
        .DescriptorLength = 9,
        .DescriptorType = Configuration,
        .TotalLength = 0x19,
        .InterfaceCount = 1,
        .ConfigurationValue = 1,
        .StringIndex = 0,
        .Attributes = {
            .RemoteWakeup = false,
            .SelfPowered = true,
            ._reserved7 = 1,
        },
        .MaximumPower = 0,
    },
    .Interface = {
        .DescriptorLength = 9,
        .DescriptorType = Interface,
        .Number = 0,
        .AlternateSetting = 0,
        .EndpointCount = 1,
        .Class = InterfaceClassHub,
        .SubClass = 0,
        .Protocol = 0,
        .StringIndex = 0,
    },
    .Endpoint = {
        .DescriptorLength = 7,
        .DescriptorType = Endpoint,
        .EndpointAddress = {
            .Number = 1,
            .Direction = In,
        },
        .Attributes = {
            .Type = Interrupt,
        },
        .Packet = {
            .MaxSize = 8,
        },
        .Interval = 0xff,
    },
};

struct UsbStringDescriptor String0 = {
    .DescriptorLength = 4,
    .DescriptorType = String,
    .Data = {
        0x0409,
    },
};

// char *str = "0x058f";
// const uint16_t result123[] = strtol(str, NULL, 16);

struct UsbStringDescriptor String1 = {
    .DescriptorLength = sizeof(L"USB 2.0 Fake Root Hub") + 2,
    .DescriptorType = String,
    .Data = u"USB 2.0 Fake Root Hub",
};

struct HubDescriptor HubDescriptor = {
    .DescriptorLength = 0x9,
    .DescriptorType = Hub,
    .PortCount = 1,
    .Attributes = {
        .PowerSwitchingMode = Global,
        .Compound = false,
        .OverCurrentProtection = Global,
        .ThinkTime = 0,
        .Indicators = false,
    },
    .PowerGoodDelay = 0,
    .MaximumHubPower = 0,
    .Data = {
        0x01,
        0xff,
    },
};

uint32_t RootHubDeviceNumber = 0;

Result HcdProcessRootHubMessage(struct UsbDevice *device,
                                struct UsbPipeAddress pipe, void *buffer, uint32_t bufferLength,
                                struct UsbDeviceRequest *request)
{
    uint32_t replyLength;
    Result result;

    result = OK;
    device->Error = Processing;

    if (pipe.Type == Interrupt)
    {
        printf("HCD.Hub: RootHub does not support IRQ pipes.\n");
        device->Error = Stall;
        return OK;
    }

    replyLength = 0;

    switch (request->Request)
    {
    case GetStatus:
        switch (request->Type)
        {
        case 0x80: //
            *(uint16_t *)buffer = 1;
            replyLength = 2;
            break;
        case 0x81: // interface
        case 0x82: // endpoint
            *(uint16_t *)buffer = 0;
            replyLength = 2;
            break;
        case 0xa0: // class
            *(uint32_t *)buffer = 0;
            replyLength = 4;
            break;
        case 0xa3:
            ReadBackReg(&Host->Port);

            *(uint32_t *)buffer = 0;
            ((struct HubPortFullStatus *)buffer)->Status.Connected = Host->Port.Connect;
            ((struct HubPortFullStatus *)buffer)->Status.Enabled = Host->Port.Enable;
            ((struct HubPortFullStatus *)buffer)->Status.Suspended = Host->Port.Suspend;
            ((struct HubPortFullStatus *)buffer)->Status.OverCurrent = Host->Port.OverCurrent;
            ((struct HubPortFullStatus *)buffer)->Status.Reset = Host->Port.Reset;
            ((struct HubPortFullStatus *)buffer)->Status.Power = Host->Port.Power;
            if (Host->Port.Speed == High)
                ((struct HubPortFullStatus *)buffer)->Status.HighSpeedAttatched = true;
            else if (Host->Port.Speed == Low)
                ((struct HubPortFullStatus *)buffer)->Status.LowSpeedAttatched = true;
            ((struct HubPortFullStatus *)buffer)->Status.TestMode = Host->Port.TestControl;
            ((struct HubPortFullStatus *)buffer)->Change.ConnectedChanged = Host->Port.ConnectDetected;
            ((struct HubPortFullStatus *)buffer)->Change.EnabledChanged = Host->Port.EnableChanged;
            ((struct HubPortFullStatus *)buffer)->Change.OverCurrentChanged = Host->Port.OverCurrentChanged;
            ((struct HubPortFullStatus *)buffer)->Change.ResetChanged = true;
            replyLength = 4;
            break;
        default:
            device->Error = Stall;
            break;
        };
        break;
    case ClearFeature:
        replyLength = 0;
        switch (request->Type)
        {
        case 0x2:
        case 0x20:
            break;
        case 0x23:
            switch ((enum HubPortFeature)request->Value)
            {
            case FeatureEnable:
                ReadBackReg(&Host->Port);
                Host->Port.Enable = true;
                WriteThroughRegMask(&Host->Port, 0x4);
                break;
            case FeatureSuspend:
                ClearReg(Power);
                WriteThroughReg(Power);
                MicroDelay(5000);
                ReadBackReg(&Host->Port);
                Host->Port.Resume = true;
                WriteThroughRegMask(&Host->Port, 0x40);
                MicroDelay(100000);
                Host->Port.Resume = false;
                Host->Port.Suspend = false;
                WriteThroughRegMask(&Host->Port, 0xc0);
                break;
            case FeaturePower:
                ReadBackReg(&Host->Port);
                Host->Port.Power = false;
                WriteThroughRegMask(&Host->Port, 0x1000);
                break;
            case FeatureConnectionChange:
                ReadBackReg(&Host->Port);
                Host->Port.ConnectDetected = true;
                WriteThroughRegMask(&Host->Port, 0x2);
                break;
            case FeatureEnableChange:
                ReadBackReg(&Host->Port);
                Host->Port.EnableChanged = true;
                WriteThroughRegMask(&Host->Port, 0x8);
                break;
            case FeatureOverCurrentChange:
                ReadBackReg(&Host->Port);
                Host->Port.OverCurrentChanged = true;
                WriteThroughRegMask(&Host->Port, 0x20);
                break;
            default:
                break;
            }
            break;
        default:
            result = ErrorArgument;
            break;
        }
        break;
    case SetFeature:
        replyLength = 0;
        switch (request->Type)
        {
        case 0x20:
            break;
        case 0x23:
            switch ((enum HubPortFeature)request->Value)
            {
            case FeatureReset:
                ReadBackReg(Power);
                Power->EnableSleepClockGating = false;
                Power->StopPClock = false;
                WriteThroughReg(Power);
                ClearReg(Power);
                WriteThroughReg(Power);

                ReadBackReg(&Host->Port);
                Host->Port.Suspend = false;
                Host->Port.Reset = true;
                Host->Port.Power = true;
                WriteThroughRegMask(&Host->Port, 0x1180);
                MicroDelay(60000);
                Host->Port.Reset = false;
                WriteThroughRegMask(&Host->Port, 0x1000);
                break;
            case FeaturePower:
                ReadBackReg(&Host->Port);
                Host->Port.Power = true;
                WriteThroughRegMask(&Host->Port, 0x1000);
                break;
            default:
                break;
            }
            break;
        default:
            result = ErrorArgument;
            break;
        }
        break;
    case SetAddress:
        replyLength = 0;
        RootHubDeviceNumber = request->Value;
        break;
    case GetDescriptor:
        switch (request->Type)
        {
        case 0x80:
            switch ((enum DescriptorType)((request->Value >> 8) & 0xff))
            {
            case Device:
                MemoryCopy(buffer, &DeviceDescriptor, replyLength = Min(sizeof(DeviceDescriptor), bufferLength, uint32_t));
                break;
            case Configuration:
                MemoryCopy(buffer, &ConfigurationDescriptor, replyLength = Min(sizeof(ConfigurationDescriptor), bufferLength, uint32_t));
                break;
            case String:
                switch (request->Value & 0xff)
                {
                case 0x0:
                    MemoryCopy(buffer, &String0, replyLength = Min(String0.DescriptorLength, bufferLength, uint32_t));
                    break;
                case 0x1:
                    MemoryCopy(buffer, &String1, replyLength = Min(String1.DescriptorLength, bufferLength, uint32_t));
                    break;
                default:
                    replyLength = 0;
                    break;
                }
                break;
            default:
                result = ErrorArgument;
            }
            break;
        case 0xa0:
            MemoryCopy(buffer, &HubDescriptor, replyLength = Min(HubDescriptor.DescriptorLength, bufferLength, uint32_t));
            break;
        default:
            result = ErrorArgument;
            break;
        }
        break;
    case GetConfiguration:
        *(uint16_t *)buffer = 0x1;
        replyLength = 1;
        break;
    case SetConfiguration:
        replyLength = 0;
        break;
    default:
        result = ErrorArgument;
        break;
    }

    if (result == ErrorArgument)
        device->Error |= Stall;

    device->Error &= ~Processing;
    device->LastTransfer = replyLength;

    return OK;
}