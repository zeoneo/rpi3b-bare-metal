#include <kernel/types.h>
#include <device/hid.h>
#include <device/hcd.h>
#include <device/uart0.h>
#include <device/usbd.h>
#include <device/usb_report.h>
#include <device/usb-mem.h>
#include <plibc/stdio.h>
#include <kernel/systimer.h>

#define HidMessageTimeout 10

Result (*HidUsageAttach[HidUsageAttachCount])(struct UsbDevice *device, uint32_t interfaceNumber);

#define InterfaceClassAttachCount 16 // TODO remove this and unify code.
extern Result (*InterfaceClassAttach[InterfaceClassAttachCount])(struct UsbDevice *device, uint32_t interfaceNumber);

void HidLoad()
{
    printf("CSUD: HID driver version 0.1\n");
    InterfaceClassAttach[InterfaceClassHid] = HidAttach;
}

Result HidAttach(struct UsbDevice *device, uint32_t interfaceNumber)
{
    struct HidDevice *data;
    struct HidDescriptor *descriptor;
    struct UsbDescriptorHeader *header;
    void *reportDescriptor = NULL;
    Result result;
    uint32_t currentInterface;
    uint8_t pollingInterval = 0;

    if (device->Interfaces[interfaceNumber].Class != InterfaceClassHid)
    {
        return ErrorArgument;
    }
    if (device->Interfaces[interfaceNumber].EndpointCount < 1)
    {
        printf("HID: Invalid HID device with fewer than one endpoints (%d).\n", device->Interfaces[interfaceNumber].EndpointCount);
        return ErrorIncompatible;
    }
    if (device->Endpoints[interfaceNumber][0].EndpointAddress.Direction != In ||
        device->Endpoints[interfaceNumber][0].Attributes.Type != Interrupt)
    {
        printf("HID: Invalid HID device with unusual endpoints (0).\n");
        return ErrorIncompatible;
    }
    if (device->Interfaces[interfaceNumber].EndpointCount >= 2)
    {
        if (device->Endpoints[interfaceNumber][1].EndpointAddress.Direction != Out ||
            device->Endpoints[interfaceNumber][1].Attributes.Type != Interrupt)
        {
            printf("HID: Invalid HID device with unusual endpoints (1).\n");
            return ErrorIncompatible;
        }
    }
    if (device->Status != Configured)
    {
        printf("HID: Cannot start driver on unconfigured device!\n");
        return ErrorDevice;
    }
    if (device->Interfaces[interfaceNumber].SubClass == 1)
    {
        if (device->Interfaces[interfaceNumber].Protocol == 1)
            printf("HID: Boot keyboard detected.\n");
        else if (device->Interfaces[interfaceNumber].Protocol == 2)
            printf("HID: Boot mouse detected.\n");
        else
            printf("HID: Unknown boot device detected.\n");

        printf("HID: Reverting from boot to normal HID mode.\n");
        if ((result = HidSetProtocol(device, interfaceNumber, 1)) != OK) // change protocol to 1 for report mode. 0 is boot mode
        {
            printf("HID: Could not revert to report mode from HID mode.\n");
            return result;
        }
        pollingInterval = device->Endpoints[interfaceNumber][0].Interval;
    }


    header = (struct UsbDescriptorHeader *)device->FullConfiguration;
    descriptor = NULL;
    currentInterface = interfaceNumber + 1; // Certainly different!
    do
    {
        if (header->DescriptorLength == 0)
            break; // List end
        switch (header->DescriptorType)
        {
        case Interface:
            currentInterface = ((struct UsbInterfaceDescriptor *)header)->Number;
            break;
        case Hid:
            if (currentInterface == interfaceNumber)
                descriptor = (void *)header;
            break;
        default:
            break;
        }
        if (descriptor != NULL)
            break;
        header = (void *)((uint8_t *)header + header->DescriptorLength);
    } while (true);

    // printf("HID_PRAKASH descriptor address: %x \n", descriptor);
    if (descriptor == NULL)
    {
        printf("HID: No HID descriptor in %s.Interface%d. Cannot be a HID device.\n", UsbGetDescription(device), interfaceNumber + 1);
        return ErrorIncompatible;
    }

    if (descriptor->HidVersion > 0x111)
    {
        printf("HID: Device uses unsupported HID version %x.%x.\n", descriptor->HidVersion >> 8, descriptor->HidVersion & 0xff);
        return ErrorIncompatible;
    }
    printf("HID: Device version HID %x.%x.\n", descriptor->HidVersion >> 8, descriptor->HidVersion & 0xff);

    device->DeviceDeallocate = HidDeallocate;
    device->DeviceDetached = HidDetached;
    if ((device->DriverData = MemoryAllocate(sizeof(struct HidDevice))) == NULL)
    {
        result = ErrorMemory;
        goto deallocate;
    }

    device->DriverData->DataSize = sizeof(struct HidDevice);
    // printf("HID_PRAKASH: debug 2. \n");
    device->DriverData->DeviceDriver = DeviceDriverHid;
    // printf("HID_PRAKASH: debug 3. \n");
    data = (struct HidDevice *)device->DriverData;
    data->pollingIntervalInMs = pollingInterval == 0 ? 4 : pollingInterval; //atleast 4ms of polling interval.
    data->Descriptor = descriptor;
    data->DriverData = NULL;

    uint16_t length_aligned = descriptor->LengthHi << 8 | descriptor->LengthLo;
    printf("HID_PRAKASH: Descriptor count :%d  \n", descriptor->DescriptorCount);
    // printf("HID_PRAKASH: debug 3.01. size:%x  \n", length_aligned);
    reportDescriptor = MemoryAllocate(length_aligned); //
    if (reportDescriptor == NULL)
    {
        // printf("HID_PRAKASH: debug 3.1. \n");
        result = ErrorMemory;
        goto deallocate;
    }

    if ((result = UsbGetDescriptor(device, HidReport, 0, interfaceNumber, reportDescriptor, length_aligned, length_aligned, 1)) != OK)
    {
        // printf("HID_PRAKASH: debug 5. \n");
        MemoryDeallocate(reportDescriptor);
        // printf("HID_PRAKASH: debug 6. \n");
        printf("HID: Could not read report descriptor for %s.Interface%d.\n", UsbGetDescription(device), interfaceNumber + 1);
        goto deallocate;
    }
    if ((result = HidParseReportDescriptor(device, reportDescriptor, length_aligned)) != OK)
    {
        // printf("HID_PRAKASH: debug 7. \n");
        MemoryDeallocate(reportDescriptor);
        // printf("HID_PRAKASH: debug 8. \n");
        printf("HID: Invalid report descriptor for %s.Interface%d.\n", UsbGetDescription(device), interfaceNumber + 1);
        goto deallocate;
    }

    // printf("HID_PRAKASH: debug 9. \n");
    MemoryDeallocate(reportDescriptor);
    // printf("HID_PRAKASH: debug     10. \n");
    reportDescriptor = NULL;

    data->ParserResult->Interface = interfaceNumber;
    if (data->ParserResult->Application.Page == GenericDesktopControl &&
        (uint16_t)data->ParserResult->Application.Desktop < HidUsageAttachCount &&
        HidUsageAttach[(uint16_t)data->ParserResult->Application.Desktop] != NULL)
    {
        HidUsageAttach[(uint16_t)data->ParserResult->Application.Desktop](device, interfaceNumber);
    }
    printf("HID_PRAK: HID ATTACH safe return. \n");
    return OK;
deallocate:
    printf("HID_PRAK: HID ATTACH DEALLOCAte. \n");
    if (reportDescriptor != NULL)
        MemoryDeallocate(reportDescriptor);
    HidDeallocate(device);
    return result;
}

void HidDeallocate(struct UsbDevice *device)
{
    struct HidDevice *data;
    struct HidParserReport *report;

    if (device->DriverData != NULL)
    {
        data = (struct HidDevice *)device->DriverData;

        if (data->HidDeallocate != NULL)
            data->HidDeallocate(device);

        if (data->ParserResult != NULL)
        {
            for (uint32_t i = 0; i < data->ParserResult->ReportCount; i++)
            {
                if (data->ParserResult->Report[i] != NULL)
                {
                    report = data->ParserResult->Report[i];
                    if (report->ReportBuffer != NULL)
                        MemoryDeallocate(report->ReportBuffer);
                    for (uint32_t j = 0; j < report->FieldCount; j++)
                        if (!report->Fields[j].Attributes.Variable)
                            MemoryDeallocate(report->Fields[j].Value.Pointer);
                    MemoryDeallocate(data->ParserResult->Report[i]);
                }
            }
            MemoryDeallocate(data->ParserResult);
        }

        MemoryDeallocate(data);
    }
    device->DeviceDeallocate = NULL;
    device->DeviceDetached = NULL;
}

void HidDetached(struct UsbDevice *device)
{
    struct HidDevice *data;

    if (device->DriverData != NULL)
    {
        data = (struct HidDevice *)device->DriverData;

        if (data->HidDetached != NULL)
            data->HidDetached(device);
    }
}

Result HidSetProtocol(struct UsbDevice *device, uint8_t interface, uint16_t protocol)
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
                 .Request = SetProtocol,
                 .Type = 0x21,
                 .Index = interface,
                 .Value = protocol,
                 .Length = 0,
             },
             HidMessageTimeout)) != OK)
        return result;

    return OK;
}

Result HidParseReportDescriptor(struct UsbDevice *device, void *descriptor, uint16_t length)
{
    Result result;
    struct HidDevice *data;
    struct HidParserResult *parse = NULL;
    struct
    {
        uint8_t reportCount;
        uint8_t indent;
        bool input, output, feature;
    } reports = {.reportCount = 0, .indent = 0, .input = false, .output = false, .feature = false};
    struct reportFields_t
    {
        uint32_t count;
        uint8_t current;
        uint8_t report;
        struct reportFields_inner_t
        {
            uint8_t reportId;
            uint8_t fieldCount;
            enum HidReportType type;
        } reports[];
    } *reportFields = NULL;
    struct fields_t
    {
        struct HidParserResult *result;
        uint32_t count;
        uint32_t size;
        struct HidFullUsage *usage;
        struct HidFullUsage physical;
        int32_t logicalMinimum;
        int32_t logicalMaximum;
        int32_t physicalMinimum;
        int32_t physicalMaximum;
        struct HidUnit unit;
        int32_t unitExponent;
        enum HidUsagePage page;
        uint8_t report;
    } *fields = NULL;
    void *usageStack = NULL;

    data = (struct HidDevice *)device->DriverData;

    HidEnumerateReport(descriptor, length, HidEnumerateActionCountReport, &reports);
    printf("HID: Found %d reports. \n", reports.reportCount);

    if ((parse = MemoryAllocate(sizeof(struct HidParserResult) + 4 * reports.reportCount)) == NULL)
    {
        result = ErrorMemory;
        goto deallocate;
    }
    for (uint32_t i = 0; i < reports.reportCount; i++)
    {
        parse->Report[i] = NULL;
    }
    if ((reportFields = MemoryAllocate(sizeof(struct reportFields_t) + sizeof(struct reportFields_inner_t) * reports.reportCount)) == NULL)
    {
        result = ErrorMemory;
        goto deallocate;
    }
    parse->ReportCount = reports.reportCount;
    reportFields->count = 0;
    reportFields->current = 0;
    reportFields->report = 0;

    HidEnumerateReport(descriptor, length, HidEnumerateActionCountField, reportFields);
    for (uint32_t i = 0; i < reports.reportCount; i++)
    {
        if ((parse->Report[i] = MemoryAllocate(sizeof(struct HidParserReport) + sizeof(struct HidParserField) * reportFields->reports[i].fieldCount)) == NULL)
        {
            result = ErrorMemory;
            goto deallocate;
        }
        parse->Report[i]->Index = i;
        parse->Report[i]->FieldCount = 0;
        parse->Report[i]->Id = reportFields->reports[i].reportId;
        parse->Report[i]->Type = reportFields->reports[i].type;
        parse->Report[i]->ReportLength = 0;
        parse->Report[i]->ReportBuffer = NULL;
    }
    MemoryDeallocate(reportFields);
    reportFields = NULL;

    if ((fields = MemoryAllocate(sizeof(struct fields_t))) == NULL)
    {
        result = ErrorMemory;
        goto deallocate;
    }
    if ((fields->usage = usageStack = MemoryAllocate(16 * sizeof(struct HidFullUsage *))) == NULL)
    {
        result = ErrorMemory;
        goto deallocate;
    }
    fields->count = 0;
    fields->logicalMaximum = 0;
    fields->logicalMinimum = 0;
    fields->physicalMaximum = 0;
    fields->physicalMinimum = 0;
    fields->report = 0;
    fields->size = 0;
    *(uint32_t *)fields->usage = 0xffffffff;
    fields->result = parse;
    HidEnumerateReport(descriptor, length, HidEnumerateActionAddField, fields);

    data->ParserResult = parse;
    parse = NULL;
    result = OK;
deallocate:
    if (usageStack != NULL)
        MemoryDeallocate(usageStack);
    if (fields != NULL)
        MemoryDeallocate(fields);
    if (reportFields != NULL)
        MemoryDeallocate(reportFields);
    if (parse != NULL)
    {
        for (uint32_t i = 0; i < reports.reportCount; i++)
            if (parse->Report[i] != NULL)
                MemoryDeallocate(parse->Report[i]);
        MemoryDeallocate(parse);
    }
    return result;
}

void HidEnumerateReport(void *descriptor, uint16_t length, void (*action)(void *data, uint16_t tag, uint32_t value), void *data)
{
    struct HidReportItem *item, *current;
    uint16_t parsedLength, currentIndex, currentLength;
    uint16_t tag; // tags for short items will be stored in the low 6 bits, tags for long items will be in the top 8 bits.
    int32_t value;

    item = descriptor;
    current = NULL;
    parsedLength = 0;

    while (parsedLength < length)
    {
        if (current == NULL)
        {
            current = item;
            currentIndex = 0;
            currentLength = 1 << (current->Size - 1);
            value = 0;
            tag = current->Tag;
            if (currentLength == 0)
                current = NULL;
        }
        else
        {
            if (current->Tag == TagLong && currentIndex < 2)
            {
                if (currentIndex == 0)
                    currentLength += *(uint8_t *)item;
                else
                    tag |= (uint16_t) * (uint8_t *)item << 8;
            }
            else
            {
                value |= (uint32_t) * (uint8_t *)item << (8 * currentIndex);
            }
            if (++currentIndex == currentLength)
                current = NULL;
        }

        if (current == NULL)
        {
            if ((tag & 0x3) == 0x1)
            {
                if (currentLength == 1 && (value & 0x80))
                    value |= 0xffffff00;
                else if (currentLength == 2 && (value & 0x8000))
                    value |= 0xffff0000;
            }

            action(data, tag, value);
        }

        item++;
        parsedLength++;
    }
}

void HidEnumerateActionCountReport(void *data, uint16_t tag, uint32_t value)
{
    struct
    {
        uint8_t reportCount;
        uint8_t indent;
        bool input, output, feature;
    } *reports = data;

    switch (tag)
    {
    case TagMainInput:
        if (!reports->input)
        {
            reports->reportCount++;
            reports->input = true;
        }
        printf("HID: %.*sInput(%03o)\n", reports->indent, "           ", value);
        break;
    case TagMainOutput:
        if (!reports->output)
        {
            reports->reportCount++;
            reports->output = true;
        }
        printf("HID: %.*sOutput(%03o)\n", reports->indent, "           ", value);
        break;
    case TagMainFeature:
        if (!reports->feature)
        {
            reports->reportCount++;
            reports->feature = true;
        }
        printf("HID: %.*sFeature(%03o)\n", reports->indent, "           ", value);
        break;
    case TagMainCollection:
        printf("HID: %.*sCollection(%d)\n", reports->indent, "           ", value);
        reports->indent++;
        break;
    case TagMainEndCollection:
        reports->indent--;
        printf("HID: %.*sEnd Collection\n", reports->indent, "           ");
        break;
    case TagGlobalUsagePage:
        printf("HID: %.*sUsage Page(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalLogicalMinimum:
        printf("HID: %.*sLogical Minimum(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalLogicalMaximum:
        printf("HID: %.*sLogical Maximum(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalPhysicalMinimum:
        printf("HID: %.*sPhysical Minimum(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalPhysicalMaximum:
        printf("HID: %.*sPhysical Maximum(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalUnitExponent:
        printf("HID: %.*sUnit Exponent(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalUnit:
        printf("HID: %.*sUnit(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalReportSize:
        printf("HID: %.*sReport Size(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalReportId:
        reports->input = reports->output = reports->feature = false;
        printf("HID: %.*sReport ID(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalReportCount:
        printf("HID: %.*sReport Count(%d)\n", reports->indent, "           ", value);
        break;
    case TagGlobalPush:
        printf("HID: %.*sPush\n", reports->indent, "           ");
        break;
    case TagGlobalPop:
        printf("HID: %.*sPop\n", reports->indent, "           ");
        break;
    case TagLocalUsage:
        printf("HID: %.*sUsage(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalUsageMinimum:
        printf("HID: %.*sUsage Minimum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalUsageMaximum:
        printf("HID: %.*sUsage Maximum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalDesignatorIndex:
        printf("HID: %.*sDesignator Index(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalDesignatorMinimum:
        printf("HID: %.*sDesignator Minimum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalDesignatorMaximum:
        printf("HID: %.*sDesignator Maximum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalStringIndex:
        printf("HID: %.*sString Index(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalStringMinimum:
        printf("HID: %.*sString Minimum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalStringMaximum:
        printf("HID: %.*sString Maximum(%u)\n", reports->indent, "           ", value);
        break;
    case TagLocalDelimiter:
        printf("HID: %.*sDelimiter\n", reports->indent, "           ");
        break;
    default:
        printf("HID: Unexpected tag in report %d = %x.\n", tag, value);
        break;
    }
}

void HidEnumerateActionCountField(void *data, uint16_t tag, uint32_t value)
{
    struct reportFields_t
    {
        uint32_t count;
        uint8_t current;
        uint8_t report;
        struct reportFields_inner_t
        {
            uint8_t reportId;
            uint8_t fieldCount;
            enum HidReportType type;
        } reports[];
    } *reportFields = data;
    struct reportFields_inner_t *inner;
    enum HidReportType type;

    type = 0;
    switch (tag)
    {
    case TagMainFeature:
        type++;
    case TagMainOutput:
        type++;
    case TagMainInput:
        type++;
        inner = NULL;
        for (uint32_t i = 0; i < reportFields->current; i++)
        {
            if (reportFields->reports[i].reportId == reportFields->report &&
                reportFields->reports[i].type == type)
            {
                inner = &reportFields->reports[i];
                break;
            }
        }
        if (inner == NULL)
        {
            inner = &reportFields->reports[reportFields->current++];
            inner->reportId = reportFields->report;
            inner->fieldCount = 0;
            inner->type = type;
        }
        struct HidMainItem *hidMainItemPtr = (struct HidMainItem *)&value;
        if (hidMainItemPtr->Variable)
            inner->fieldCount += reportFields->count;
        else
            inner->fieldCount++;
        break;
    case TagGlobalReportCount:
        reportFields->count = value;
        break;
    case TagGlobalReportId:
        reportFields->report = value;
        break;
    default:
        break;
    }
}

void HidEnumerateActionAddField(void *data, uint16_t tag, uint32_t value)
{
    struct fields_t
    {
        struct HidParserResult *result;
        uint32_t count;
        uint32_t size;
        struct HidFullUsage *usage;
        struct HidFullUsage physical;
        int32_t logicalMinimum;
        int32_t logicalMaximum;
        int32_t physicalMinimum;
        int32_t physicalMaximum;
        struct HidUnit unit;
        int32_t unitExponent;
        enum HidUsagePage page;
        uint8_t report;
    } *fields = data;
    enum HidReportType type;
    struct HidParserReport *report;
    uint32_t i;

    uint32_t *fUsefulPtr = NULL;

    type = 0;
    switch (tag)
    {
    case TagMainFeature:
        type++;
    case TagMainOutput:
        type++;
    case TagMainInput:
        type++;
        report = NULL;
        for (i = 0; i < fields->result->ReportCount; i++)
            if (fields->result->Report[i]->Id == fields->report &&
                fields->result->Report[i]->Type == type)
            {
                report = fields->result->Report[i];
                break;
            }
        while (fields->count > 0)
        {
            if (*(uint32_t *)fields->usage == 0xffffffff)
            {
                fields->usage++;
            }
            uint32_t *attrPtrPrk = (uint32_t *)&(report->Fields[report->FieldCount].Attributes);
            *attrPtrPrk = value;
            report->Fields[report->FieldCount].Count = report->Fields[report->FieldCount].Attributes.Variable ? 1 : fields->count;
            report->Fields[report->FieldCount].LogicalMaximum = fields->logicalMaximum;
            report->Fields[report->FieldCount].LogicalMinimum = fields->logicalMinimum;
            report->Fields[report->FieldCount].Offset = report->ReportLength;
            report->Fields[report->FieldCount].PhysicalMaximum = fields->physicalMaximum;
            report->Fields[report->FieldCount].PhysicalMinimum = fields->physicalMinimum;

            uint32_t *physicalUsagePtrPrk = (uint32_t *)&(report->Fields[report->FieldCount].PhysicalUsage);
            uint32_t *physicalRvalPrk = (uint32_t *)&(fields->physical);
            *physicalUsagePtrPrk = *physicalRvalPrk;
            report->Fields[report->FieldCount].Size = fields->size;
            uint32_t *x = (uint32_t *)&(report->Fields[report->FieldCount].Unit);
            uint32_t *yPtr = (uint32_t *)&(fields->unit);
            *x = *yPtr;
            report->Fields[report->FieldCount].UnitExponent = fields->unitExponent;
            if ((uint16_t)fields->usage->Page == 0xffff)
            {
                uint32_t *x = (uint32_t *)&(report->Fields[report->FieldCount].Usage);
                *x = *(uint32_t *)&fields->usage[-1];
                if (fields->usage->Desktop == fields->usage[-1].Desktop ||
                    !report->Fields[report->FieldCount].Attributes.Variable)
                    fields->usage -= 2;
                else
                    fields->usage[-1].Desktop++;
            }
            else
            {
                uint32_t *x = (uint32_t *)&(report->Fields[report->FieldCount].Usage);
                *x = *(uint32_t *)(fields->usage--);
            }
            if (report->Fields[report->FieldCount].Attributes.Variable)
            {
                fields->count--;
                report->ReportLength += report->Fields[report->FieldCount].Size;
                report->Fields[report->FieldCount].Value.U32 = 0;
            }
            else
            {
                fields->count = 0;
                report->ReportLength += report->Fields[report->FieldCount].Size * report->Fields[report->FieldCount].Count;
                report->Fields[report->FieldCount].Value.Pointer = MemoryAllocate(report->Fields[report->FieldCount].Size * report->Fields[report->FieldCount].Count / 8);
            }
            report->FieldCount++;
        }
        *(uint32_t *)&fields->usage[1] = 0;
        break;
    case TagMainCollection:
        if (*(uint32_t *)fields->usage == 0xffffffff)
            fields->usage++;
        switch ((enum HidMainCollection)value)
        {
        case Application:
            fUsefulPtr = (uint32_t *)&fields->result->Application;
            *fUsefulPtr = *(uint32_t *)fields->usage;
            break;
        case Physical:
            fUsefulPtr = (uint32_t *)&fields->physical;
            *fUsefulPtr = *(uint32_t *)fields->usage;
            break;
        default:
            break;
        }
        fields->usage--;
        break;
    case TagMainEndCollection:
        switch ((enum HidMainCollection)value)
        {
        case Physical:
            fUsefulPtr = (uint32_t *)&(fields->physical);
            *fUsefulPtr = 0;
            break;
        default:
            break;
        }
        break;
    case TagGlobalUsagePage:
        fields->page = (enum HidUsagePage)value;
        break;
    case TagGlobalLogicalMinimum:
        fields->logicalMinimum = value;
        break;
    case TagGlobalLogicalMaximum:
        fields->logicalMaximum = value;
        break;
    case TagGlobalPhysicalMinimum:
        fields->physicalMinimum = value;
        break;
    case TagGlobalPhysicalMaximum:
        fields->physicalMaximum = value;
        break;
    case TagGlobalUnitExponent:
        fields->unitExponent = value;
        break;
    case TagGlobalUnit:
        fUsefulPtr = (uint32_t *)&(fields->unit);
        *fUsefulPtr = value;
        break;
    case TagGlobalReportSize:
        fields->size = value;
        break;
    case TagGlobalReportId:
        fields->report = (uint8_t)value;
        break;
    case TagGlobalReportCount:
        fields->count = value;
        break;
    case TagLocalUsage:
        fields->usage++;
        if (value & 0xffff0000)
        {
            uint32_t *fUsagePtr = (uint32_t *)&(fields->usage);
            *fUsagePtr = value;
        }
        else
        {
            fields->usage->Desktop = (enum HidUsagePageDesktop)value;
            fields->usage->Page = fields->page;
        }
        break;
    case TagLocalUsageMinimum:
        fields->usage++;
        if (value & 0xffff0000)
        {
            uint32_t *fUsagePtr = (uint32_t *)&(fields->usage);
            *fUsagePtr = value;
        }
        else
        {
            fields->usage->Desktop = (enum HidUsagePageDesktop)value;
            fields->usage->Page = fields->page;
        }
        break;
    case TagLocalUsageMaximum:
        fields->usage++;
        fields->usage->Desktop = (enum HidUsagePageDesktop)value;
        fields->usage->Page = (enum HidUsagePage)0xffff;
        break;
    default:
        break;
    }
}

Result HidReadDevice(struct UsbDevice *device, uint8_t reportNumber)
{
    struct HidDevice *data;
    struct HidParserResult *parse;
    struct HidParserReport *report;
    struct HidParserField *field;
    Result result;
    uint32_t size;

    data = (struct HidDevice *)device->DriverData;
    parse = data->ParserResult;
    report = parse->Report[reportNumber];
    size = ((report->ReportLength + 7) / 8);
    if ((report->ReportBuffer == NULL) && (report->ReportBuffer = (uint8_t *)MemoryAllocate(size)) == NULL)
    {
        return ErrorMemory;
    }

    // printf("Report buffer: size:%d ", size);
    int i1 = 0;
    uint8_t *temp_buf = (uint8_t *)report->ReportBuffer;
    while (i1 < size)
    {
        *temp_buf = 0x0;
        // printf("%x ", *temp_buf);
        temp_buf++;
        i1++;
    }
    // printf("\n");

    // printf("HID_BEFORE: %s.Report%d: %02x%02x%02x%02x %02x%02x%02x%02x.\n", UsbGetDescription(device), reportNumber + 1,
    // 	*(report->ReportBuffer + 0), *(report->ReportBuffer + 1), *(report->ReportBuffer + 2), *(report->ReportBuffer + 3),
    // 	*(report->ReportBuffer + 4), *(report->ReportBuffer + 5), *(report->ReportBuffer + 6), *(report->ReportBuffer + 7));

    // printf("Getting report from report type: %d report id:%d interface:%d size: %d buffer:%x \n", report->Type, report->Id, data->ParserResult->Interface, size, report->ReportBuffer);
    if ((result = HidGetReport(device, report->Type, report->Id, data->ParserResult->Interface, size, report->ReportBuffer)) != OK)
    {
        if (result != ErrorDisconnected)
            printf("HID: Could not read %s report %d.\n", UsbGetDescription(device), report);
        return result;
    }

    // Uncomment this for a quick hack to view 8 bytes worth of report.

    // printf("HID_AFTER: %s.Report%d: %02x%02x%02x%02x %02x%02x%02x%02x.\n", UsbGetDescription(device), reportNumber + 1,
    // 	*(report->ReportBuffer + 0), *(report->ReportBuffer + 1), *(report->ReportBuffer + 2), *(report->ReportBuffer + 3));

    for (uint32_t i = 0; i < report->FieldCount; i++)
    {
        field = &report->Fields[i];
        if (field->Attributes.Variable)
        {
            if (field->LogicalMinimum < 0)
                field->Value.S32 = BitGetSigned(report->ReportBuffer, field->Offset, field->Size);
            else
                field->Value.U32 = BitGetUnsigned(report->ReportBuffer, field->Offset, field->Size);
        }
        else
        {
            for (uint32_t j = 0; j < field->Count; j++)
            {
                BitSet(
                    field->Value.Pointer,
                    j * field->Size,
                    field->Size,
                    field->LogicalMinimum < 0 ? BitGetSigned(
                                                    report->ReportBuffer,
                                                    field->Offset + j * field->Size,
                                                    field->Size)
                                              : BitGetUnsigned(
                                                    report->ReportBuffer,
                                                    field->Offset + j * field->Size,
                                                    field->Size));
            }
        }
    }

    return OK;
}

Result HidWriteDevice(struct UsbDevice *device, uint8_t reportNumber)
{
    struct HidDevice *data;
    struct HidParserResult *parse;
    struct HidParserReport *report;
    struct HidParserField *field;
    Result result;
    uint32_t size;

    data = (struct HidDevice *)device->DriverData;
    parse = data->ParserResult;
    report = parse->Report[reportNumber];
    size = ((report->ReportLength + 7) / 8);
    if ((report->ReportBuffer == NULL) && (report->ReportBuffer = (uint8_t *)MemoryAllocate(size)) == NULL)
    {
        return ErrorMemory;
    }
    for (uint32_t i = 0; i < report->FieldCount; i++)
    {
        field = &report->Fields[i];
        if (field->Attributes.Variable)
        {
            BitSet(
                report->ReportBuffer,
                field->Offset,
                field->Size,
                field->Value.S32);
        }
        else
        {
            for (uint32_t j = 0; j < field->Count; j++)
            {
                BitSet(
                    report->ReportBuffer,
                    field->Offset + j * field->Size,
                    field->Size,
                    BitGetSigned(
                        field->Value.Pointer,
                        j * field->Size,
                        field->Size));
            }
        }
    }

    if ((result = HidSetReport(device, report->Type, report->Id, data->ParserResult->Interface, size, report->ReportBuffer)) != OK)
    {
        if (result != ErrorDisconnected)
            printf("HID: Coult not read %s report %d.\n", UsbGetDescription(device), report);
        return result;
    }

    return OK;
}

int32_t HidGetFieldValue(struct HidParserField *field, uint32_t index)
{
    return BitGetSigned(field->Value.Pointer, index * field->Size, field->Size);
}

void BitSet(void *buffer, uint32_t offset, uint32_t length, uint32_t value)
{
    uint8_t *bitBuffer;
    uint8_t mask;

    bitBuffer = buffer;
    for (uint32_t i = offset / 8, j = 0; i < (offset + length + 7) / 8; i++)
    {
        if (offset / 8 == (offset + length - 1) / 8)
        {
            mask = (1 << ((offset % 8) + length)) - (1 << (offset % 8));
            bitBuffer[i] = (bitBuffer[i] & ~mask) |
                           ((value << (offset % 8)) & mask);
        }
        else if (i == offset / 8)
        {
            mask = 0x100 - (1 << (offset % 8));
            bitBuffer[i] = (bitBuffer[i] & ~mask) |
                           ((value << (offset % 8)) & mask);
            j += 8 - (offset % 8);
        }
        else if (i == (offset + length - 1) / 8)
        {
            mask = (1 << ((offset % 8) + length)) - 1;
            bitBuffer[i] = (bitBuffer[i] & ~mask) |
                           ((value >> j) & mask);
        }
        else
        {
            bitBuffer[i] = (value >> j) & 0xff;
            j += 8;
        }
    }
}

Result HidGetReport(struct UsbDevice *device, enum HidReportType reportType,
                    uint8_t reportId, __attribute__((__unused__)) uint8_t interface, uint32_t bufferLength, void *buffer)
{
    Result result;
    struct HidDevice *  hidDevice = (struct HidDevice *)device->DriverData;
    while (1)
    {
        if ((result = HcdInterruptPoll(
                 device,
                 (struct UsbPipeAddress){
                     .Type = Interrupt,
                     .Speed = device->Speed,
                     .EndPoint = 1,
                     .Device = device->Number,
                     .Direction = In,
                     .MaxSize = SizeFromNumber(device->Descriptor.MaxPacketSize0),
                 },
                 buffer,
                 bufferLength,
                 &(struct UsbDeviceRequest){
                     .Request = GetReport,
                     .Type = 0xa1,
                     .Index = Endpoint,
                     .Value = (uint16_t)reportType << 8 | reportId,
                     .Length = bufferLength,
                 })) == OK)
        {
            // We received input report from device.
            return result;
        }
        MicroDelay((hidDevice->pollingIntervalInMs) * 1000);
    }

    return result;
}

Result HidSetReport(struct UsbDevice *device, enum HidReportType reportType,
                    uint8_t reportId, uint8_t interface, uint32_t bufferLength, void *buffer)
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
             buffer,
             bufferLength,
             &(struct UsbDeviceRequest){
                 .Request = SetReport,
                 .Type = 0x21,
                 .Index = interface,
                 .Value = (uint16_t)reportType << 8 | reportId,
                 .Length = bufferLength,
             },
             HidMessageTimeout)) != OK)
        return result;

    return OK;
}

uint32_t BitGetUnsigned(void *buffer, uint32_t offset, uint32_t length)
{
    uint8_t *bitBuffer;
    uint8_t mask;
    uint32_t result;

    bitBuffer = buffer;
    result = 0;
    for (uint32_t i = offset / 8, j = 0; i < (offset + length + 7) / 8; i++)
    {
        if (offset / 8 == (offset + length - 1) / 8)
        {
            mask = (1 << ((offset % 8) + length)) - (1 << (offset % 8));
            result = (bitBuffer[i] & mask) >> (offset % 8);
        }
        else if (i == offset / 8)
        {
            mask = 0x100 - (1 << (offset % 8));
            j += 8 - (offset % 8);
            result = ((bitBuffer[i] & mask) >> (offset % 8)) << (length - j);
        }
        else if (i == (offset + length - 1) / 8)
        {
            mask = (1 << ((offset % 8) + length)) - 1;
            result |= bitBuffer[i] & mask;
        }
        else
        {
            j += 8;
            result |= bitBuffer[i] << (length - j);
        }
    }

    return result;
}

int32_t BitGetSigned(void *buffer, uint32_t offset, uint32_t length)
{
    uint32_t result = BitGetUnsigned(buffer, offset, length);

    if (result & (1 << (length - 1)))
        result |= 0xffffffff - ((1 << length) - 1);

    return result;
}
