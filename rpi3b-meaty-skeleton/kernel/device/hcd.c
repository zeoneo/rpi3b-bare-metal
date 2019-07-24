#include <device/hcd.h>
#include <device/usbd.h>
#include <device/roothub.h>
#include <device/usb-mem.h>
#include <klib/printk.h>
#include <stdint.h>
#include <kernel/rpi-mailbox-interface.h>
#include <kernel/systimer.h>
#include <kernel/types.h>

bool PhyInitialised = false;
volatile struct CoreGlobalRegs *CorePhysical, *Core = NULL;
volatile struct HostGlobalRegs *HostPhysical, *Host = NULL;
volatile struct PowerReg *PowerPhysical, *Power = NULL;
uint8_t *databuffer = NULL;

extern void dmb(void);
uint32_t usb_hcd_device_id = 0x3;

Result HcdChannelSendInterruptPoll(struct UsbDevice *device,
                                   struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, uint32_t bufferLength,
                                   struct UsbDeviceRequest *request, enum PacketId packetId);
Result HcdChannelSendInterruptPollOne(struct UsbDevice *device,
                                      struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, __attribute__((__unused__)) uint32_t bufferLength, uint32_t bufferOffset,
                                      struct UsbDeviceRequest *request);

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

    printk("\n device id: %x", (uint32_t)(mp->data.buffer_32[0]));
    printk("\n device state: %x", (uint32_t)(mp->data.buffer_32[1]));
}

int power_on_host_controller()
{
    turn_on_usb();
    print_usb_power_state();
    uint32_t *vendor_id = (void *)(USB_CORE_BASE + RegVendorId);
    uint32_t *user_id = (void *)(USB_CORE_BASE + RegUserId);

    if ((*vendor_id & 0xfffff000) != POWER_ON_VENDOR_ID)
    {
        printk("\n Host controller with expected vendor id not found. vendor_id: %x ", *vendor_id);
        return -1;
    }
    if (*user_id != POWER_ON_USB_USER_ID)
    {
        printk("\n Host controller with expected user id not found. user_id: %x ", *user_id);
        return -1;
    }
    return 0;
}

int hcd_start()
{
    Result result;
    uint32_t timeout;

    printk("\n-----HCD START----begins----------\n");

    power_on_host_controller();

    printk("HCD: Start core.\n");

    if (Core == NULL)
    {
        printk("HCD: HCD uninitialised. Cannot be started.\n");
        return ErrorDevice;
    }

    if ((databuffer = MemoryAllocate(1024)) == NULL)
        return ErrorMemory;

    ReadBackReg(&Core->Usb);
    Core->Usb.UlpiDriveExternalVbus = 0;
    Core->Usb.TsDlinePulseEnable = 0;
    WriteThroughReg(&Core->Usb);

    printk("HCD: Master reset.\n");
    if ((result = HcdReset()) != OK)
    {
        goto deallocate;
    }

    if (!PhyInitialised)
    {
        printk("HCD: One time phy initialisation.\n");
        PhyInitialised = true;

        Core->Usb.ModeSelect = UTMI;
        printk("HCD: Interface: UTMI+.\n");
        Core->Usb.PhyInterface = false;

        WriteThroughReg(&Core->Usb);
        HcdReset();
    }

    ReadBackReg(&Core->Usb);
    if (Core->Hardware.HighSpeedPhysical == Ulpi && Core->Hardware.FullSpeedPhysical == Dedicated)
    {
        printk("HCD: ULPI FSLS configuration: enabled.\n");
        Core->Usb.UlpiFsls = true;
        Core->Usb.ulpi_clk_sus_m = true;
    }
    else
    {
        printk("HCD: ULPI FSLS configuration: disabled.\n");
        Core->Usb.UlpiFsls = false;
        Core->Usb.ulpi_clk_sus_m = false;
    }
    WriteThroughReg(&Core->Usb);

    printk("\n-----HCD START----ends----------\n");

    ReadBackReg(&Core->Usb);
    if (Core->Hardware.HighSpeedPhysical == Ulpi && Core->Hardware.FullSpeedPhysical == Dedicated)
    {
        printk("HCD: ULPI FSLS configuration: enabled.\n");
        Core->Usb.UlpiFsls = true;
        Core->Usb.ulpi_clk_sus_m = true;
    }
    else
    {
        printk("HCD: ULPI FSLS configuration: disabled.\n");
        Core->Usb.UlpiFsls = false;
        Core->Usb.ulpi_clk_sus_m = false;
    }
    WriteThroughReg(&Core->Usb);

    printk("HCD: DMA configuration: enabled.\n");
    ReadBackReg(&Core->Ahb);
    Core->Ahb.DmaEnable = true;
    Core->Ahb.DmaRemainderMode = Incremental;
    WriteThroughReg(&Core->Ahb);

    ReadBackReg(&Core->Usb);
    switch (Core->Hardware.OperatingMode)
    {
    case HNP_SRP_CAPABLE:
        printk("HCD: HNP/SRP configuration: HNP, SRP.\n");
        Core->Usb.HnpCapable = true;
        Core->Usb.SrpCapable = true;
        break;
    case SRP_ONLY_CAPABLE:
    case SRP_CAPABLE_DEVICE:
    case SRP_CAPABLE_HOST:
        printk("HCD: HNP/SRP configuration: SRP.\n");
        Core->Usb.HnpCapable = false;
        Core->Usb.SrpCapable = true;
        break;
    case NO_HNP_SRP_CAPABLE:
    case NO_SRP_CAPABLE_DEVICE:
    case NO_SRP_CAPABLE_HOST:
        printk("HCD: HNP/SRP configuration: none.\n");
        Core->Usb.HnpCapable = false;
        Core->Usb.SrpCapable = false;
        break;
    }
    WriteThroughReg(&Core->Usb);
    printk("HCD: Core started.\n");
    printk("HCD: Starting host.\n");

    ClearReg(Power);
    WriteThroughReg(Power);

    ReadBackReg(&Host->Config);
    if (Core->Hardware.HighSpeedPhysical == Ulpi && Core->Hardware.FullSpeedPhysical == Dedicated && Core->Usb.UlpiFsls)
    {
        printk("HCD: Host clock: 48Mhz.\n");
        Host->Config.ClockRate = Clock48MHz;
    }
    else
    {
        printk("HCD: Host clock: 30-60Mhz.\n");
        Host->Config.ClockRate = Clock30_60MHz;
    }
    WriteThroughReg(&Host->Config);

    ReadBackReg(&Host->Config);
    Host->Config.FslsOnly = true;
    WriteThroughReg(&Host->Config);

    ReadBackReg(&Host->Config);
    if (Host->Config.EnableDmaDescriptor ==
            Core->Hardware.DmaDescription &&
        (Core->VendorId & 0xfff) >= 0x90a)
    {
        printk("HCD: DMA descriptor: enabled.\n");
    }
    else
    {
        printk("HCD: DMA descriptor: disabled.\n");
    }
    WriteThroughReg(&Host->Config);

    printk("HCD: FIFO configuration: Total=%x Rx=%x NPTx=%x PTx=%x.\n", ReceiveFifoSize + NonPeriodicFifoSize + PeriodicFifoSize, ReceiveFifoSize, NonPeriodicFifoSize, PeriodicFifoSize);
    ReadBackReg(&Core->Receive.Size);
    Core->Receive.Size = ReceiveFifoSize;
    WriteThroughReg(&Core->Receive.Size);

    ReadBackReg(&Core->NonPeriodicFifo.Size);
    Core->NonPeriodicFifo.Size.Depth = NonPeriodicFifoSize;
    Core->NonPeriodicFifo.Size.StartAddress = ReceiveFifoSize;
    WriteThroughReg(&Core->NonPeriodicFifo.Size);

    ReadBackReg(&Core->PeriodicFifo.HostSize);
    Core->PeriodicFifo.HostSize.Depth = PeriodicFifoSize;
    Core->PeriodicFifo.HostSize.StartAddress = ReceiveFifoSize + NonPeriodicFifoSize;
    WriteThroughReg(&Core->PeriodicFifo.HostSize);

    printk("HCD: Set HNP: enabled.\n");
    ReadBackReg(&Core->OtgControl);
    Core->OtgControl.HostSetHnpEnable = true;
    WriteThroughReg(&Core->OtgControl);

    if ((result = HcdTransmitFifoFlush(FlushAll)) != OK)
        goto deallocate;
    if ((result = HcdReceiveFifoFlush()) != OK)
        goto deallocate;

    if (!Host->Config.EnableDmaDescriptor)
    {
        for (uint32_t channel = 0; channel < Core->Hardware.HostChannelCount; channel++)
        {
            ReadBackReg(&Host->Channel[channel].Characteristic);
            Host->Channel[channel].Characteristic.Enable = false;
            Host->Channel[channel].Characteristic.Disable = true;
            Host->Channel[channel].Characteristic.EndPointDirection = In;
            WriteThroughReg(&Host->Channel[channel].Characteristic);
            timeout = 0;
        }

        // Halt channels to put them into known state.
        for (uint32_t channel = 0; channel < Core->Hardware.HostChannelCount; channel++)
        {
            ReadBackReg(&Host->Channel[channel].Characteristic);
            Host->Channel[channel].Characteristic.Enable = true;
            Host->Channel[channel].Characteristic.Disable = true;
            Host->Channel[channel].Characteristic.EndPointDirection = In;
            WriteThroughReg(&Host->Channel[channel].Characteristic);
            timeout = 0;
            do
            {
                ReadBackReg(&Host->Channel[channel].Characteristic);

                if (timeout++ > 0x100000)
                {
                    printk("HCD: Unable to clear halt on channel %u.\n", channel);
                }
            } while (Host->Channel[channel].Characteristic.Enable);
        }
    }

    ReadBackReg(&Host->Port);
    if (!Host->Port.Power)
    {
        printk("HCD: Powering up port.\n");
        Host->Port.Power = true;
        WriteThroughRegMask(&Host->Port, 0x1000);
    }

    printk("HCD: Reset port.\n");
    ReadBackReg(&Host->Port);
    Host->Port.Reset = true;
    WriteThroughRegMask(&Host->Port, 0x100);

    uint64_t timer_tick = timer_getTickCount64();
    while (timer_getTickCount64() < (timer_tick + 50000))
        ;
    Host->Port.Reset = false;
    WriteThroughRegMask(&Host->Port, 0x100);
    ReadBackReg(&Host->Port);

    printk("HCD: Successfully started.\n");

    return OK;

deallocate:
    MemoryDeallocate(databuffer);
    return 0;
}

Result HcdStart()
{
    Result result = hcd_start();
    if (result != OK)
    {
        printk("Could not start HCD");
        return result;
    }

    if ((result = UsbAttachRootHub()) != OK)
    {
        printk("USBD: Failed to enumerate devices.\n");
        goto errorStop;
    }

    return result;
errorStop:
    // HcdStop();
    // errorDeinitialise:
    //     HcdDeinitialise();
    // errorReturn:
    return result;

    return ErrorGeneral;
}

Result HcdInitialize()
{
    volatile Result result = OK;
    if (sizeof(struct CoreGlobalRegs) != 0x400 || sizeof(struct HostGlobalRegs) != 0x400 || sizeof(struct PowerReg) != 0x4)
    {
        printk("HCD: Incorrectly compiled driver. HostGlobalRegs: %x (0x400), CoreGlobalRegs: %x (0x400), PowerReg: %x (0x4).\n",
               sizeof(struct HostGlobalRegs), sizeof(struct CoreGlobalRegs), sizeof(struct PowerReg));
        result = ErrorCompiler; // Correct packing settings are required.
    }
    else
    {
        printk("\n HCD: registers allocated proper memory");
    }

    CorePhysical = MemoryReserve(sizeof(struct CoreGlobalRegs), HCD_DESIGNWARE_BASE);
    Core = MemoryAllocate(sizeof(struct CoreGlobalRegs));

    HostPhysical = MemoryReserve(sizeof(struct HostGlobalRegs), (void *)((uint8_t *)HCD_DESIGNWARE_BASE + 0x400));
    Host = MemoryAllocate(sizeof(struct HostGlobalRegs));

    PowerPhysical = MemoryReserve(sizeof(struct PowerReg), (void *)((uint8_t *)HCD_DESIGNWARE_BASE + 0xe00));
    Power = MemoryAllocate(sizeof(struct PowerReg));

    ReadBackReg(&Core->VendorId);
    ReadBackReg(&Core->UserId);

    ReadBackReg(&Core->Hardware);

    if (Core->Hardware.Architecture != InternalDma)
    {
        printk("HCD: Host architecture is not Internal DMA. Driver incompatible.\n");
        result = ErrorIncompatible;
        goto deallocate;
    }

    printk("HCD: Internal DMA mode.\n");
    if (Core->Hardware.HighSpeedPhysical == NotSupported)
    {
        printk("HCD: High speed physical unsupported. Driver incompatible.\n");
        result = ErrorIncompatible;
        goto deallocate;
    }
    // printk("HCD: Hardware configuration: %x %x %x %x\n", *(uint32_t *)&Core->Hardware, *((uint32_t *)&Core->Hardware + 1), *((uint32_t *)&Core->Hardware + 2), *((uint32_t *)&Core->Hardware + 3));
    ReadBackReg(&Host->Config);
    // printk("HCD: Host configuration: %08x\n", *(uint32_t *)&Host->Config);

    printk("HCD: Disabling USB interrupts.\n");
    ReadBackReg(&Core->Ahb);
    Core->Ahb.InterruptEnable = false;
    ClearReg(&Core->InterruptMask);
    WriteThroughReg(&Core->InterruptMask);
    WriteThroughReg(&Core->Ahb);

    printk("HCD: Load completed.\n");

    return OK;
deallocate:
    if (Core != NULL)
        MemoryDeallocate((void *)Core);
    if (Host != NULL)
        MemoryDeallocate((void *)Host);
    if (Power != NULL)
        MemoryDeallocate((void *)Power);
    return result;

    return result;
}

/** 
	\brief Triggers the core soft reset.

	Raises the core soft reset signal high, and then waits for the core to 
	signal that it is ready again.
*/
Result HcdReset()
{
    uint32_t count = 0;

    do
    {
        ReadBackReg(&Core->Reset);
        if (count++ >= 0x100000)
        {
            printk("HCD: Device Hang!\n");
            return ErrorDevice;
        }
    } while (Core->Reset.AhbMasterIdle == false);

    Core->Reset.CoreSoft = true;
    WriteThroughReg(&Core->Reset);
    count = 0;

    do
    {
        ReadBackReg(&Core->Reset);
        if (count++ >= 0x100000)
        {
            printk("HCD: Device Hang!\n");
            return ErrorDevice;
        }
    } while (Core->Reset.CoreSoft == true || Core->Reset.AhbMasterIdle == false);

    return OK;
}

void WriteThroughReg(volatile const void *reg)
{
    WriteThroughRegMask(reg, 0);
}

void WriteThroughRegMask(volatile const void *reg, uint32_t maskOr)
{
    if ((uint32_t)reg - (uint32_t)Core < sizeof(struct CoreGlobalRegs))
    {
        maskOr |= 0xffffffff;
        *(uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical) = *((uint32_t *)reg) & maskOr;
    }
    else if ((uint32_t)reg - (uint32_t)Host < sizeof(struct HostGlobalRegs))
    {
        switch ((uint32_t)reg - (uint32_t)Host)
        {
        case 0x40: // Host->Port
            maskOr |= 0x1f140;
            break;
        default:
            maskOr |= 0xffffffff;
            break;
        }
        *(uint32_t *)((uint32_t)reg - (uint32_t)Host + (uint32_t)HostPhysical) = *((uint32_t *)reg) & maskOr;
    }
    else if ((uint32_t)reg == (uint32_t)Power)
    {
        maskOr |= 0xffffffff;
        *(uint32_t *)PowerPhysical = *(uint32_t *)Power & maskOr;
    }
}

void ReadBackReg(volatile const void *reg)
{
    if ((uint32_t)reg - (uint32_t)Core < sizeof(struct CoreGlobalRegs))
    {
        switch ((uint32_t)reg - (uint32_t)Core)
        {
        case 0x44: // Core->Hardware
            *((uint32_t *)reg + 0) = *((uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical) + 0);
            *((uint32_t *)reg + 1) = *((uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical) + 1);
            *((uint32_t *)reg + 2) = *((uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical) + 2);
            *((uint32_t *)reg + 3) = *((uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical) + 3);
            break;
        default:
            *(uint32_t *)reg = *(uint32_t *)((uint32_t)reg - (uint32_t)Core + (uint32_t)CorePhysical);
        }
    }
    else if ((uint32_t)reg - (uint32_t)Host < sizeof(struct HostGlobalRegs))
    {
        *(uint32_t *)reg = *(uint32_t *)((uint32_t)reg - (uint32_t)Host + (uint32_t)HostPhysical);
    }
    else if ((uint32_t)reg == (uint32_t)Power)
    {
        *(uint32_t *)Power = *(uint32_t *)PowerPhysical;
    }
}

void ClearReg(volatile const void *reg)
{
    if ((uint32_t)reg - (uint32_t)Core < sizeof(struct CoreGlobalRegs))
    {
        switch ((uint32_t)reg - (uint32_t)Core)
        {
        case 0x44: // Core->Hardware
            *((uint32_t *)reg + 0) = 0;
            *((uint32_t *)reg + 1) = 0;
            *((uint32_t *)reg + 2) = 0;
            *((uint32_t *)reg + 3) = 0;
            break;
        default:
            *(uint32_t *)reg = 0;
        }
    }
    else if ((uint32_t)reg - (uint32_t)Host < sizeof(struct HostGlobalRegs))
    {
        *(uint32_t *)reg = 0;
    }
    else if ((uint32_t)reg == (uint32_t)Power)
    {
        *(uint32_t *)Power = 0;
    }
}
void SetReg(volatile const void *reg)
{
    uint32_t value;
    if ((uint32_t)reg - (uint32_t)Core < sizeof(struct CoreGlobalRegs))
    {
        value = 0xffffffff;
        switch ((uint32_t)reg - (uint32_t)Core)
        {
        case 0x44: // Core->Hardware
            *((uint32_t *)reg + 0) = value;
            *((uint32_t *)reg + 1) = value;
            *((uint32_t *)reg + 2) = value;
            *((uint32_t *)reg + 3) = value;
            break;
        default:
            *(uint32_t *)reg = value;
        }
    }
    else if ((uint32_t)reg - (uint32_t)Host < sizeof(struct HostGlobalRegs))
    {
        if ((uint32_t)reg - (uint32_t)Host > 0x100 && (uint32_t)reg - (uint32_t)Host < 0x300)
        {
            switch (((uint32_t)reg - (uint32_t)Host) & 0x1f)
            {
            case 0x8:
                value = 0x3fff;
                break;
            default:
                value = 0xffffffff;
                break;
            }
        }
        else
            value = 0xffffffff;

        *(uint32_t *)reg = value;
    }
    else if ((uint32_t)reg == (uint32_t)Power)
    {
        value = 0xffffffff;
        *(uint32_t *)Power = value;
    }
}

/** 
	\brief Triggers the fifo flush for a given fifo.

	Raises the core fifo flush signal high, and then waits for the core to 
	signal that it is ready again.
*/

Result HcdTransmitFifoFlush(enum CoreFifoFlush fifo)
{
    uint32_t count = 0;

    if (fifo == FlushAll)
        printk("HCD: TXFlush(All)\n");
    else if (fifo == FlushNonPeriodic)
        printk("HCD: TXFlush(NP)\n");
    else
        printk("HCD: TXFlush(P%u)\n", fifo);

    ClearReg(&Core->Reset);
    Core->Reset.TransmitFifoFlushNumber = fifo;
    Core->Reset.TransmitFifoFlush = true;
    WriteThroughReg(&Core->Reset);
    count = 0;

    do
    {
        ReadBackReg(&Core->Reset);
        if (count++ >= 0x100000)
        {
            printk("HCD: Device Hang!\n");
            return ErrorDevice;
        }
    } while (Core->Reset.TransmitFifoFlush == true);

    return OK;
}

/** 
	\brief Triggers the receive fifo flush for a given fifo.

	Raises the core receive fifo flush signal high, and then waits for the core to 
	signal that it is ready again.
*/
Result HcdReceiveFifoFlush()
{
    uint32_t count = 0;

    printk("HCD: RXFlush(All)\n");

    ClearReg(&Core->Reset);
    Core->Reset.ReceiveFifoFlush = true;
    WriteThroughReg(&Core->Reset);
    count = 0;

    do
    {
        ReadBackReg(&Core->Reset);
        if (count++ >= 0x100000)
        {
            printk("HCD: Device Hang!\n");
            return ErrorDevice;
        }
    } while (Core->Reset.ReceiveFifoFlush == true);

    return OK;
}

extern uint32_t RootHubDeviceNumber;

Result HcdSumbitInterruptOutMessage(struct UsbDevice *device,
                                    struct UsbPipeAddress pipe, void *buffer, uint32_t bufferLength,
                                    struct UsbDeviceRequest *request)
{
    Result result;
    struct UsbPipeAddress tempPipe;
    if (pipe.Device == RootHubDeviceNumber)
    {
        return HcdProcessRootHubMessage(device, pipe, buffer, bufferLength, request);
    }

    device->Error = Processing;
    device->LastTransfer = 0;

    // Data
    if (buffer != NULL)
    {
        if (pipe.Direction == Out)
        {
            MemoryCopy(databuffer, buffer, bufferLength);
        }
        tempPipe.Speed = pipe.Speed;
        tempPipe.Device = pipe.Device;
        tempPipe.EndPoint = pipe.EndPoint;
        tempPipe.MaxSize = pipe.MaxSize;
        tempPipe.Type = Interrupt;
        tempPipe.Direction = Out;

        if ((result = HcdChannelSendWait(device, &tempPipe, 0, databuffer, bufferLength, request, Data0)) != OK)
        {
            printk("HCD: Could not send DATA to %s.\n", UsbGetDescription(device));
            return OK;
        }

        ReadBackReg(&Host->Channel[0].TransferSize);
        if (Host->Channel[0].TransferSize.TransferSize <= bufferLength)
        {
            printk("Data transferred : %d \n ", Host->Channel[0].TransferSize.TransferSize);
            device->LastTransfer = bufferLength - Host->Channel[0].TransferSize.TransferSize;
        }
        else
        {
            printk("HCD: Weird transfer.. %d/%d bytes received.\n", Host->Channel[0].TransferSize.TransferSize, bufferLength);
            printk("HCD: Message %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x ...\n",
                   ((uint8_t *)databuffer)[0x0], ((uint8_t *)databuffer)[0x1], ((uint8_t *)databuffer)[0x2], ((uint8_t *)databuffer)[0x3],
                   ((uint8_t *)databuffer)[0x4], ((uint8_t *)databuffer)[0x5], ((uint8_t *)databuffer)[0x6], ((uint8_t *)databuffer)[0x7],
                   ((uint8_t *)databuffer)[0x8], ((uint8_t *)databuffer)[0x9], ((uint8_t *)databuffer)[0xa], ((uint8_t *)databuffer)[0xb],
                   ((uint8_t *)databuffer)[0xc], ((uint8_t *)databuffer)[0xd], ((uint8_t *)databuffer)[0xe], ((uint8_t *)databuffer)[0xf]);
            device->LastTransfer = bufferLength;
        }
        MemoryCopy(buffer, databuffer, device->LastTransfer);
    }

    // Status
    tempPipe.Speed = pipe.Speed;
    tempPipe.Device = pipe.Device;
    tempPipe.EndPoint = pipe.EndPoint;
    tempPipe.MaxSize = pipe.MaxSize;
    tempPipe.Type = Interrupt;
    tempPipe.Direction = Out;

    if ((result = HcdChannelSendWait(device, &tempPipe, 0, databuffer, 0, request, Data1)) != OK)
    {
        printk("HCD: Could not send STATUS to %s.\n", UsbGetDescription(device));
        return OK;
    }

    ReadBackReg(&Host->Channel[0].TransferSize);
    if (Host->Channel[0].TransferSize.TransferSize != 0)
        printk("HCD: Warning non zero status transfer! %d.\n", Host->Channel[0].TransferSize.TransferSize);

    device->Error = NoError;

    return OK;
}

Result HcdSumbitControlMessage(struct UsbDevice *device,
                               struct UsbPipeAddress pipe, void *buffer, uint32_t bufferLength,
                               struct UsbDeviceRequest *request)
{
    Result result;
    struct UsbPipeAddress tempPipe;
    if (pipe.Device == RootHubDeviceNumber)
    {
        return HcdProcessRootHubMessage(device, pipe, buffer, bufferLength, request);
    }

    device->Error = Processing;
    device->LastTransfer = 0;

    // Setup
    tempPipe.Speed = pipe.Speed;
    tempPipe.Device = pipe.Device;
    tempPipe.EndPoint = pipe.EndPoint;
    tempPipe.MaxSize = pipe.MaxSize;
    tempPipe.Type = Control;
    tempPipe.Direction = Out;

    if ((result = HcdChannelSendWait(device, &tempPipe, 0, request, 8, request, Setup)) != OK)
    {
        printk("HCD: Could not send SETUP to %s.\n", UsbGetDescription(device));
        return OK;
    }

    // Data
    if (buffer != NULL)
    {
        if (pipe.Direction == Out)
        {
            MemoryCopy(databuffer, buffer, bufferLength);
        }
        tempPipe.Speed = pipe.Speed;
        tempPipe.Device = pipe.Device;
        tempPipe.EndPoint = pipe.EndPoint;
        tempPipe.MaxSize = pipe.MaxSize;
        tempPipe.Type = Control;
        tempPipe.Direction = pipe.Direction;

        if ((result = HcdChannelSendWait(device, &tempPipe, 0, databuffer, bufferLength, request, Data1)) != OK)
        {
            printk("HCD: Could not send DATA to %s.\n", UsbGetDescription(device));
            return OK;
        }

        ReadBackReg(&Host->Channel[0].TransferSize);
        if (pipe.Direction == In)
        {
            if (Host->Channel[0].TransferSize.TransferSize <= bufferLength)
                device->LastTransfer = bufferLength - Host->Channel[0].TransferSize.TransferSize;
            else
            {
                printk("HCD: Weird transfer.. %d/%d bytes received.\n", Host->Channel[0].TransferSize.TransferSize, bufferLength);
                printk("HCD: Message %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x ...\n",
                       ((uint8_t *)databuffer)[0x0], ((uint8_t *)databuffer)[0x1], ((uint8_t *)databuffer)[0x2], ((uint8_t *)databuffer)[0x3],
                       ((uint8_t *)databuffer)[0x4], ((uint8_t *)databuffer)[0x5], ((uint8_t *)databuffer)[0x6], ((uint8_t *)databuffer)[0x7],
                       ((uint8_t *)databuffer)[0x8], ((uint8_t *)databuffer)[0x9], ((uint8_t *)databuffer)[0xa], ((uint8_t *)databuffer)[0xb],
                       ((uint8_t *)databuffer)[0xc], ((uint8_t *)databuffer)[0xd], ((uint8_t *)databuffer)[0xe], ((uint8_t *)databuffer)[0xf]);
                device->LastTransfer = bufferLength;
            }
            MemoryCopy(buffer, databuffer, device->LastTransfer);
        }
        else
        {
            device->LastTransfer = bufferLength;
        }
    }

    // Status
    tempPipe.Speed = pipe.Speed;
    tempPipe.Device = pipe.Device;
    tempPipe.EndPoint = pipe.EndPoint;
    tempPipe.MaxSize = pipe.MaxSize;
    tempPipe.Type = Control;
    tempPipe.Direction = ((bufferLength == 0) || pipe.Direction == Out) ? In : Out;

    if ((result = HcdChannelSendWait(device, &tempPipe, 0, databuffer, 0, request, Data1)) != OK)
    {
        printk("HCD: Could not send STATUS to %s.\n", UsbGetDescription(device));
        return OK;
    }

    ReadBackReg(&Host->Channel[0].TransferSize);
    if (Host->Channel[0].TransferSize.TransferSize != 0)
        printk("HCD: Warning non zero status transfer! %d.\n", Host->Channel[0].TransferSize.TransferSize);

    device->Error = NoError;

    return OK;
}

Result HcdInterruptPoll(struct UsbDevice *device,
                        struct UsbPipeAddress pipe, void *buffer, uint32_t bufferLength,
                        struct UsbDeviceRequest *request)
{
    Result result;
    struct UsbPipeAddress tempPipe = {0};

    device->Error = Processing;
    device->LastTransfer = 0;

    // Data stage
    // 1. IN Token
    // 2. Receive Data from device or Receive NAK/STALL status
    // 3. If we receive data then send ACK to device

    uint8_t *temp_buf = (uint8_t *)databuffer;
    int i1 = 0;
    while (i1 < 1024)
    {
        *temp_buf = 0x0;
        temp_buf++;
        i1++;
    }

    tempPipe.Speed = pipe.Speed;
    tempPipe.Device = pipe.Device;
    tempPipe.EndPoint = pipe.EndPoint;
    tempPipe.MaxSize = pipe.MaxSize;
    tempPipe.Type = Interrupt;
    tempPipe.Direction = In;

    result = HcdChannelSendInterruptPoll(device, &tempPipe, 0, databuffer, bufferLength, request, InPid);
    if (result == ErrorNACK || result == ErrorNYET)
    {
        // Either endpoint do not have data to send to host or we are polling interrupt endpoint to fast.
        return result;
    }
    else if (result != OK)
    {
        return result;
    }

    ReadBackReg(&Host->Channel[0].TransferSize);
    if (Host->Channel[0].TransferSize.TransferSize <= bufferLength)
    {
        device->LastTransfer = bufferLength - Host->Channel[0].TransferSize.TransferSize;
    }
    else
    {
        printk("HCD: Weird transfer.. %d/%d bytes received.\n", Host->Channel[0].TransferSize.TransferSize, bufferLength);
        printk("HCD: Message %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x ...\n",
               ((uint8_t *)databuffer)[0x0], ((uint8_t *)databuffer)[0x1], ((uint8_t *)databuffer)[0x2], ((uint8_t *)databuffer)[0x3],
               ((uint8_t *)databuffer)[0x4], ((uint8_t *)databuffer)[0x5], ((uint8_t *)databuffer)[0x6], ((uint8_t *)databuffer)[0x7],
               ((uint8_t *)databuffer)[0x8], ((uint8_t *)databuffer)[0x9], ((uint8_t *)databuffer)[0xa], ((uint8_t *)databuffer)[0xb],
               ((uint8_t *)databuffer)[0xc], ((uint8_t *)databuffer)[0xd], ((uint8_t *)databuffer)[0xe], ((uint8_t *)databuffer)[0xf]);
        device->LastTransfer = bufferLength;
    }
    MemoryCopy(buffer, databuffer, device->LastTransfer);

    device->Error = NoError;
    return OK;
}

Result HcdChannelSendWait(struct UsbDevice *device,
                          struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, uint32_t bufferLength,
                          struct UsbDeviceRequest *request, enum PacketId packetId)
{
    Result result;
    uint32_t packets, transfer, tries;

    tries = 0;
retry:
    if (tries++ == 3)
    {
        printk("HCD: Failed to send to %s after 3 attempts.\n", UsbGetDescription(device));
        return ErrorTimeout;
    }

    if ((result = HcdPrepareChannel(device, channel, bufferLength, packetId, pipe)) != OK)
    {
        device->Error = ConnectionError;
        printk("HCD: Could not prepare data channel to %s.\n", UsbGetDescription(device));
        return result;
    }

    transfer = 0;
    do
    {
        packets = Host->Channel[channel].TransferSize.PacketCount;
        if ((result = HcdChannelSendWaitOne(device, pipe, channel, buffer, bufferLength, transfer, request)) != OK)
        {
            if (result == ErrorRetry)
            {
                printk("Need to retry the packet");
                goto retry;
            }
            return result;
        }

        ReadBackReg(&Host->Channel[channel].TransferSize);
        transfer = bufferLength - Host->Channel[channel].TransferSize.TransferSize;
        if (packets == Host->Channel[channel].TransferSize.PacketCount)
        {
            break;
        }
    } while (Host->Channel[channel].TransferSize.PacketCount > 0);

    if (packets == Host->Channel[channel].TransferSize.PacketCount)
    {
        device->Error = ConnectionError;
        printk("HCD: Transfer to %s got stuck.\n", UsbGetDescription(device));
        return ErrorDevice;
    }

    if (tries > 1)
    {
        printk("HCD: Transfer to %s succeeded on attempt %d/3.\n", UsbGetDescription(device), tries);
    }

    return OK;
}

Result HcdPrepareChannel(struct UsbDevice *device, uint8_t channel,
                         uint32_t length, enum PacketId type, struct UsbPipeAddress *pipe)
{
    if (channel > Core->Hardware.HostChannelCount)
    {
        printk("HCD: Channel %d is not available on this host.\n", channel);
        return ErrorArgument;
    }

    // Clear all existing interrupts.
    SetReg(&Host->Channel[channel].Interrupt);
    WriteThroughReg(&Host->Channel[channel].Interrupt);

    // Program the channel.
    ClearReg(&Host->Channel[channel].Characteristic);
    Host->Channel[channel].Characteristic.DeviceAddress = pipe->Device;
    Host->Channel[channel].Characteristic.EndPointNumber = pipe->EndPoint;
    Host->Channel[channel].Characteristic.EndPointDirection = pipe->Direction;
    Host->Channel[channel].Characteristic.LowSpeed = pipe->Speed == Low ? true : false;
    Host->Channel[channel].Characteristic.Type = pipe->Type;
    Host->Channel[channel].Characteristic.MaximumPacketSize = SizeToNumber(pipe->MaxSize);
    Host->Channel[channel].Characteristic.Enable = false;
    Host->Channel[channel].Characteristic.Disable = false;
    WriteThroughReg(&Host->Channel[channel].Characteristic);

    // Clear split control.
    ClearReg(&Host->Channel[channel].SplitControl);
    if (pipe->Speed != High)
    {
        // printk("HCD: Prepare channel enable split control. \n");
        Host->Channel[channel].SplitControl.SplitEnable = true;
        Host->Channel[channel].SplitControl.HubAddress = device->Parent->Number;
        Host->Channel[channel].SplitControl.PortAddress = device->PortNumber;
    }
    WriteThroughReg(&Host->Channel[channel].SplitControl);

    ClearReg(&Host->Channel[channel].TransferSize);
    Host->Channel[channel].TransferSize.TransferSize = length;
    if (pipe->Speed == Low)
        Host->Channel[channel].TransferSize.PacketCount = (length + 7) / 8;
    else
        Host->Channel[channel].TransferSize.PacketCount = (length + Host->Channel[channel].Characteristic.MaximumPacketSize - 1) / Host->Channel[channel].Characteristic.MaximumPacketSize;
    if (Host->Channel[channel].TransferSize.PacketCount == 0)
        Host->Channel[channel].TransferSize.PacketCount = 1;
    Host->Channel[channel].TransferSize.PacketId = type;
    WriteThroughReg(&Host->Channel[channel].TransferSize);

    return OK;
}

//------------------------New Interrupt Send

Result HcdChannelSendInterruptPoll(struct UsbDevice *device,
                                   struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, uint32_t bufferLength,
                                   struct UsbDeviceRequest *request, enum PacketId packetId)
{
    Result result;
    uint32_t packets, transfer, tries;

    tries = 0;
retry:
    if (tries++ == 3)
    {
        printk("HCD: Failed to send to %s after 3 attempts.\n", UsbGetDescription(device));
        return ErrorTimeout;
    }

    if ((result = HcdPrepareChannel(device, channel, bufferLength, packetId, pipe)) != OK)
    {
        device->Error = ConnectionError;
        printk("HCD: Could not prepare data channel to %s.\n", UsbGetDescription(device));
        return result;
    }

    transfer = 0;
    do
    {
        packets = Host->Channel[channel].TransferSize.PacketCount;
        if ((result = HcdChannelSendInterruptPollOne(device, pipe, channel, buffer, bufferLength, transfer, request)) != OK)
        {
            if (result == ErrorRetry)
            {
                printk("Need to retry the packet");
                goto retry;
            }
            return result;
        }

        ReadBackReg(&Host->Channel[channel].TransferSize);
        transfer = bufferLength - Host->Channel[channel].TransferSize.TransferSize;
        if (packets == Host->Channel[channel].TransferSize.PacketCount)
        {
            break;
        }
    } while (Host->Channel[channel].TransferSize.PacketCount > 0);

    if (packets == Host->Channel[channel].TransferSize.PacketCount)
    {
        device->Error = ConnectionError;
        printk("HCD: Transfer to %s got stuck.\n", UsbGetDescription(device));
        return ErrorDevice;
    }

    if (tries > 1)
    {
        printk("HCD: Transfer to %s succeeded on attempt %d/3.\n", UsbGetDescription(device), tries);
    }

    return OK;
}

Result HcdChannelSendInterruptPollOne(struct UsbDevice *device,
                                      struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, __attribute__((__unused__)) uint32_t bufferLength, uint32_t bufferOffset,
                                      struct UsbDeviceRequest *request)
{
    Result result;
    uint32_t timeout, tries, globalTries, actualTries;

    for (globalTries = 0, actualTries = 0; globalTries < 3 && actualTries < 10; globalTries++, actualTries++)
    {
        SetReg(&Host->Channel[channel].Interrupt);
        WriteThroughReg(&Host->Channel[channel].Interrupt);
        ReadBackReg(&Host->Channel[channel].TransferSize);
        ReadBackReg(&Host->Channel[channel].SplitControl);

        HcdTransmitChannel(channel, (uint8_t *)buffer + bufferOffset);

        timeout = 0;
        do
        {
            if (timeout++ == RequestTimeout)
            {
                printk("HCD: Request to %s has timed out.\n", UsbGetDescription(device));
                device->Error = ConnectionError;
                return ErrorTimeout;
            }
            ReadBackReg(&Host->Channel[channel].Interrupt);
            if (!Host->Channel[channel].Interrupt.Halt)
                MicroDelay(10);
            else
                break;
        } while (true);
        ReadBackReg(&Host->Channel[channel].TransferSize);

        if (Host->Channel[channel].SplitControl.SplitEnable)
        {
            // printk("HCD: Pipe maxSize:%d speed:%d endpoint:%d device:%d transfer type:%d direction:%d \n",
            //    pipe->MaxSize, pipe->Speed, pipe->EndPoint, pipe->Device, pipe->Type, pipe->Direction);
            // printk("HCD: Working split enable %s.\n", UsbGetDescription(device));
            if (Host->Channel[channel].Interrupt.Acknowledgement)
            {
                // printk("HCD: Working split enable. ACK  %s.\n", UsbGetDescription(device));
                for (tries = 0; tries < 3; tries++)
                {
                    SetReg(&Host->Channel[channel].Interrupt);
                    WriteThroughReg(&Host->Channel[channel].Interrupt);

                    ReadBackReg(&Host->Channel[channel].SplitControl);
                    Host->Channel[channel].SplitControl.CompleteSplit = true;
                    WriteThroughReg(&Host->Channel[channel].SplitControl);

                    Host->Channel[channel].Characteristic.Enable = true;
                    Host->Channel[channel].Characteristic.Disable = false;
                    WriteThroughReg(&Host->Channel[channel].Characteristic);

                    timeout = 0;
                    do
                    {
                        if (timeout++ == RequestTimeout)
                        {
                            printk("HCD: Request split completion to %s has timed out.\n", UsbGetDescription(device));
                            device->Error = ConnectionError;
                            return ErrorTimeout;
                        }
                        ReadBackReg(&Host->Channel[channel].Interrupt);
                        if (!Host->Channel[channel].Interrupt.Halt)
                            MicroDelay(100);
                        else
                            break;
                    } while (true);
                    if (!Host->Channel[channel].Interrupt.NotYet)
                        break;
                }

                if (tries == 3)
                {
                    MicroDelay(25000);
                    continue;
                }
                else if (Host->Channel[channel].Interrupt.NegativeAcknowledgement)
                {
                    // In interrupt polling we might get NACK at times. It means device has not interrupt data to send.
                    return ErrorNACK;
                }
                else if (Host->Channel[channel].Interrupt.NotYet)
                {
                    // In interrupt polling we might get NYET at times. It means we are polling interrupt endpoint mercilessly. Slow down.
                    return ErrorNYET;
                }
                else if (Host->Channel[channel].Interrupt.TransactionError)
                {
                    printk("HCD: TransactionError error. Retrying \n");
                    MicroDelay(25000);
                    continue;
                }

                // printk("HCD: Working split enable checking interrupts.  %s.\n", UsbGetDescription(device));
                if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, false)) != OK)
                {

                    printk("HCDI1: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
                           ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
                           ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);

                    printk("HCDI1: Request split completion to %s failed.\n", UsbGetDescription(device));

                    return result;
                }
            }
            else if (Host->Channel[channel].Interrupt.NegativeAcknowledgement)
            {
                printk("HCD: Working split enable %s NACK.\n", UsbGetDescription(device));
                globalTries--;
                MicroDelay(25000);
                continue;
            }
            else if (Host->Channel[channel].Interrupt.TransactionError)
            {
                printk("HCD: Working split enable %s TRANsCA ERROR.\n", UsbGetDescription(device));
                MicroDelay(25000);
                continue;
            }
        }
        else
        {
            // printk("HCD: Working not split transaction  %s.\n", UsbGetDescription(device));
            if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, !Host->Channel[channel].SplitControl.SplitEnable)) != OK)
            {
                printk("HCDI2: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
                       ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
                       ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);
                printk("HCDI2: Request to %s failed.\n", UsbGetDescription(device));
                return ErrorRetry;
            }
        }

        break;
    }

    if (globalTries == 3 || actualTries == 10)
    {
        if(Host->Channel[channel].Interrupt.NotYet) {
            return  ErrorNYET;
        }
        if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, !Host->Channel[channel].SplitControl.SplitEnable)) != OK)
        {
            printk("HCDI3: Request to %s has failed 3 times.\n", UsbGetDescription(device));
            // printk("HCDI3: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
            //        ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
            //        ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);
            // printk("HCDI3: Request to %s failed.\n", UsbGetDescription(device));
            return result;
        }
        device->Error = ConnectionError;
        return ErrorTimeout;
    }

    return OK;
}

//-------------

Result HcdChannelSendWaitOne(struct UsbDevice *device,
                             struct UsbPipeAddress *pipe, uint8_t channel, void *buffer, __attribute__((__unused__)) uint32_t bufferLength, uint32_t bufferOffset,
                             struct UsbDeviceRequest *request)
{
    Result result;
    uint32_t timeout, tries, globalTries, actualTries;

    for (globalTries = 0, actualTries = 0; globalTries < 3 && actualTries < 10; globalTries++, actualTries++)
    {
        SetReg(&Host->Channel[channel].Interrupt);
        WriteThroughReg(&Host->Channel[channel].Interrupt);
        ReadBackReg(&Host->Channel[channel].TransferSize);
        ReadBackReg(&Host->Channel[channel].SplitControl);

        HcdTransmitChannel(channel, (uint8_t *)buffer + bufferOffset);

        timeout = 0;
        do
        {
            if (timeout++ == RequestTimeout)
            {
                printk("HCD: Request to %s has timed out.\n", UsbGetDescription(device));
                device->Error = ConnectionError;
                return ErrorTimeout;
            }
            ReadBackReg(&Host->Channel[channel].Interrupt);
            if (!Host->Channel[channel].Interrupt.Halt)
                MicroDelay(10);
            else
                break;
        } while (true);
        ReadBackReg(&Host->Channel[channel].TransferSize);

        if (Host->Channel[channel].SplitControl.SplitEnable)
        {
            // printk("HCD: Pipe maxSize:%d speed:%d endpoint:%d device:%d transfer type:%d direction:%d \n",
            //    pipe->MaxSize, pipe->Speed, pipe->EndPoint, pipe->Device, pipe->Type, pipe->Direction);
            // printk("HCD: Working split enable %s.\n", UsbGetDescription(device));
            if (Host->Channel[channel].Interrupt.Acknowledgement)
            {
                // printk("HCD: Working split enable. ACK  %s.\n", UsbGetDescription(device));
                for (tries = 0; tries < 3; tries++)
                {
                    SetReg(&Host->Channel[channel].Interrupt);
                    WriteThroughReg(&Host->Channel[channel].Interrupt);

                    ReadBackReg(&Host->Channel[channel].SplitControl);
                    Host->Channel[channel].SplitControl.CompleteSplit = true;
                    WriteThroughReg(&Host->Channel[channel].SplitControl);

                    Host->Channel[channel].Characteristic.Enable = true;
                    Host->Channel[channel].Characteristic.Disable = false;
                    WriteThroughReg(&Host->Channel[channel].Characteristic);

                    timeout = 0;
                    do
                    {
                        if (timeout++ == RequestTimeout)
                        {
                            printk("HCD: Request split completion to %s has timed out.\n", UsbGetDescription(device));
                            device->Error = ConnectionError;
                            return ErrorTimeout;
                        }
                        ReadBackReg(&Host->Channel[channel].Interrupt);
                        if (!Host->Channel[channel].Interrupt.Halt)
                            MicroDelay(100);
                        else
                            break;
                    } while (true);
                    if (!Host->Channel[channel].Interrupt.NotYet)
                        break;
                }

                if (tries == 3)
                {
                    MicroDelay(25000);
                    continue;
                }
                else if (Host->Channel[channel].Interrupt.NegativeAcknowledgement)
                {
                    printk("HCD: NAK got for split transactions. \n");
                    globalTries--;
                    MicroDelay(25000);
                    continue;
                }
                else if (Host->Channel[channel].Interrupt.TransactionError)
                {
                    printk("HCD: TransactionError error. Retrying \n");
                    MicroDelay(25000);
                    continue;
                }

                // printk("HCD: Working split enable checking interrupts.  %s.\n", UsbGetDescription(device));
                if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, false)) != OK)
                {

                    printk("HCD1: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
                           ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
                           ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);

                    printk("HCD1: Request split completion to %s failed.\n", UsbGetDescription(device));

                    return result;
                }
            }
            else if (Host->Channel[channel].Interrupt.NegativeAcknowledgement)
            {
                printk("HCD: Working split enable %s NACK.\n", UsbGetDescription(device));
                globalTries--;
                MicroDelay(25000);
                continue;
            }
            else if (Host->Channel[channel].Interrupt.TransactionError)
            {
                printk("HCD: Working split enable %s TRANsCA ERROR.\n", UsbGetDescription(device));
                MicroDelay(25000);
                continue;
            }
        }
        else
        {
            // printk("HCD: Working not split transaction  %s.\n", UsbGetDescription(device));
            if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, !Host->Channel[channel].SplitControl.SplitEnable)) != OK)
            {
                printk("HCD2: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
                       ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
                       ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);
                printk("HCD2: Request to %s failed.\n", UsbGetDescription(device));
                return ErrorRetry;
            }
        }

        break;
    }

    if (globalTries == 3 || actualTries == 10)
    {
        printk("HCD3: Request to %s has failed 3 times.\n", UsbGetDescription(device));
        if ((result = HcdChannelInterruptToError(device, Host->Channel[channel].Interrupt, !Host->Channel[channel].SplitControl.SplitEnable)) != OK)
        {
            printk("HCD3: Control message to %x: %02x%02x%02x%02x %02x%02x%02x%02x.\n", *(uint32_t *)pipe,
                   ((uint8_t *)request)[0], ((uint8_t *)request)[1], ((uint8_t *)request)[2], ((uint8_t *)request)[3],
                   ((uint8_t *)request)[4], ((uint8_t *)request)[5], ((uint8_t *)request)[6], ((uint8_t *)request)[7]);
            printk("HCD3: Request to %s failed.\n", UsbGetDescription(device));
            return result;
        }
        device->Error = ConnectionError;
        return ErrorTimeout;
    }

    return OK;
}

void HcdTransmitChannel(uint8_t channel, void *buffer)
{
    ReadBackReg(&Host->Channel[channel].SplitControl);
    Host->Channel[channel].SplitControl.CompleteSplit = false;
    WriteThroughReg(&Host->Channel[channel].SplitControl);

    if (((uint32_t)buffer & 3) != 0)
        printk("HCD: Transfer buffer %x is not DWORD aligned. Ignored, but dangerous.\n", buffer);

    Host->Channel[channel].DmaAddress = (void *)((uint32_t)buffer | (uint32_t)0xC0000000);
    WriteThroughReg(&Host->Channel[channel].DmaAddress);

    ReadBackReg(&Host->Channel[channel].Characteristic);
    Host->Channel[channel].Characteristic.PacketsPerFrame = 1;
    Host->Channel[channel].Characteristic.Enable = true;
    Host->Channel[channel].Characteristic.Disable = false;
    WriteThroughReg(&Host->Channel[channel].Characteristic);
}

Result HcdChannelInterruptToError(struct UsbDevice *device, struct ChannelInterrupts interrupts, bool isComplete)
{
    Result result;

    result = OK;
    if (interrupts.AhbError)
    {
        device->Error = AhbError;
        printk("HCD: AHB error in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.Stall)
    {
        device->Error = Stall;
        printk("HCD: Stall error in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.NegativeAcknowledgement)
    {
        device->Error = NoAcknowledge;
        printk("HCD: NAK error in transfer.\n");
        return ErrorDevice;
    }
    if (!interrupts.Acknowledgement)
    {
        printk("HCD: Transfer was not acknowledged.\n");
        result = ErrorTimeout;
    }
    if (interrupts.NotYet)
    {
        device->Error = NotYetError;
        printk("HCD: Not yet error in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.BabbleError)
    {
        device->Error = Babble;
        printk("HCD: Babble error in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.FrameOverrun)
    {
        device->Error = BufferError;
        printk("HCD: Frame overrun in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.DataToggleError)
    {
        device->Error = BitError;
        printk("HCD: Data toggle error in transfer.\n");
        return ErrorDevice;
    }
    if (interrupts.TransactionError)
    {
        device->Error = ConnectionError;
        printk("HCD: Transaction error in transfer.\n");
        return ErrorDevice;
    }
    if (!interrupts.TransferComplete && isComplete)
    {
        printk("HCD: Transfer did not complete.\n");
        result = ErrorTimeout;
    }
    return result;
}