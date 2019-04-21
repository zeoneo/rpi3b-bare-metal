#include <kernel/types.h>
#include <device/keyboard.h>
#include <device/hid.h>
#include <device/usb_report.h>
#include <device/usb-mem.h>
#include <plibc/stdio.h>

#define KeyboardMaxKeyboards 4

uint32_t keyboardCount __attribute__((aligned(4))) = 0;
uint32_t keyboardAddresses[KeyboardMaxKeyboards] = {0, 0, 0, 0};
uint32_t old_keys_down[6] = {0, 0, 0, 0, 0, 0};
struct UsbDevice *keyboards[KeyboardMaxKeyboards];

static const char keymap[256][2] __attribute__((aligned(4))) = {
    [4] = {'a', 'A'},
    [5] = {'b', 'B'},
    [6] = {'c', 'C'},
    [7] = {'d', 'D'},
    [8] = {'e', 'E'},
    [9] = {'f', 'F'},
    [10] = {'g', 'G'},
    [11] = {'h', 'H'},
    [12] = {'i', 'I'},
    [13] = {'j', 'J'},
    [14] = {'k', 'K'},
    [15] = {'l', 'L'},
    [16] = {'m', 'M'},
    [17] = {'n', 'N'},
    [18] = {'o', 'O'},
    [19] = {'p', 'P'},
    [20] = {'q', 'Q'},
    [21] = {'r', 'R'},
    [22] = {'s', 'S'},
    [23] = {'t', 'T'},
    [24] = {'u', 'U'},
    [25] = {'v', 'V'},
    [26] = {'w', 'W'},
    [27] = {'x', 'X'},
    [28] = {'y', 'Y'},
    [29] = {'z', 'Z'},
    [30] = {'1', '!'},
    [31] = {'2', '@'},
    [32] = {'3', '#'},
    [33] = {'4', '$'},
    [34] = {'5', '%'},
    [35] = {'6', '^'},
    [36] = {'7', '&'},
    [37] = {'8', '*'},
    [38] = {'9', '('},
    [39] = {'0', ')'},
    [40] = {'\n', '\n'}, /* Enter      */
    /* ... */
    [42] = {'\b', '\b'}, /* Backspace  */
    [43] = {'\t', '\t'}, /* Tab        */
    [44] = {' ', ' '},   /* Space      */
    [45] = {'-', '_'},
    [46] = {'=', '+'},
    [47] = {'[', '{'},
    [48] = {']', '}'},
    [49] = {'\\', '|'},
    /* ... */
    [51] = {';', ':'},
    [52] = {'\'', '"'},
    [53] = {'`', '~'},
    [54] = {',', '<'},
    [55] = {'.', '>'},
    [56] = {'/', '?'},
    [57] = {}, /* Caps lock  */
    /* ... */
    [76] = {'\x7f', '\x7f'}, /* Delete                    */
    /* ... */
    [84] = {'/'},  /* Keypad /                  */
    [85] = {'*'},  /* Keypad *                  */
    [86] = {'-'},  /* Keypad -                  */
    [87] = {'+'},  /* Keypad +                  */
    [88] = {'\n'}, /* Keypad Enter              */
    [89] = {'1'},  /* Keypad 1 and End          */
    [90] = {'2'},  /* Keypad 2 and Down Arrow   */
    [91] = {'3'},  /* ...                       */
    [92] = {'4'},
    [93] = {'5'},
    [94] = {'6'},
    [95] = {'7'},
    [96] = {'8'},
    [97] = {'9'},
    [98] = {'0'},
    [99] = {'.', '\x7f'}, /* Keypad . and Delete       */
    [103] = {'=', '='},   /* Keypad =                  */
};

void KbdLoad()
{
    printf("CSUD: Keyboard driver version 0.1\n");
    keyboardCount = 0;
    for (uint32_t i = 0; i < KeyboardMaxKeyboards; i++)
    {
        keyboardAddresses[i] = 0;
        keyboards[i] = NULL;
    }
    HidUsageAttach[DesktopKeyboard] = KeyboardAttach;
}

uint32_t KeyboardIndex(uint32_t address)
{
    if (address == 0)
        return 0xffffffff;

    for (uint32_t i = 0; i < keyboardCount; i++)
        if (keyboardAddresses[i] == address)
            return i;

    return 0xffffffff;
}

void KeyboardUpdate(uint32_t address)
{
    KeyboardPoll(address);
    for (uint32_t i = 0; i < 6; i++)
    {
        old_keys_down[i] = KeyboardGetKeyDown(address, i);
    }
}

bool KeyWasDown(uint16_t scanCode)
{
    for (uint32_t i = 0; i < 6; i++)
    {
        if (scanCode == old_keys_down[i])
        {
            return true;
        }
    }
    return false;
}

uint8_t KeyboardGetChar(uint32_t address)
{
    for (uint32_t i = 0; i < 6; i++)
    {
        uint16_t scanCode = KeyboardGetKeyDown(address, i);
        // printf("KeyboardGetKeyDown: %x", scanCode);
        if (scanCode == 0)
        {
            return 0;
        }
        if (!KeyWasDown(scanCode) || scanCode == 104)
        {
            continue;
        }
        // printf("key was down: %d \n", scanCode);
        struct KeyboardModifiers modifiers = KeyboardGetModifiers(address);
        if (modifiers.LeftShift == 0b1 || modifiers.RightShift == 0b1)
        {
            return keymap[scanCode][1];
        }
        else
        {
            return keymap[scanCode][0];
        }
    }
    return 0;
}

uint32_t KeyboardGetAddress(uint32_t index)
{
    if (index > keyboardCount)
        return 0;

    // for (uint32_t i = 0; index >= 0 && i < KeyboardMaxKeyboards; i++)
    for (uint32_t i = 0; i < KeyboardMaxKeyboards; i++)
    {
        if (keyboardAddresses[i] != 0)
            if (index-- == 0)
                return keyboardAddresses[i];
    }

    return 0;
}

void KeyboardDetached(struct UsbDevice *device)
{
    struct KeyboardDevice *data;

    data = (struct KeyboardDevice *)((struct HidDevice *)device->DriverData)->DriverData;
    if (data != NULL)
    {
        if (keyboardAddresses[data->Index] == device->Number)
        {
            keyboardAddresses[data->Index] = 0;
            keyboardCount--;
            keyboards[data->Index] = NULL;
        }
    }
}

void KeyboardDeallocate(struct UsbDevice *device)
{
    struct KeyboardDevice *data;

    data = (struct KeyboardDevice *)((struct HidDevice *)device->DriverData)->DriverData;
    if (data != NULL)
    {
        MemoryDeallocate(data);
        ((struct HidDevice *)device->DriverData)->DriverData = NULL;
    }
    ((struct HidDevice *)device->DriverData)->HidDeallocate = NULL;
    ((struct HidDevice *)device->DriverData)->HidDetached = NULL;
}

Result KeyboardAttach(struct UsbDevice *device, __attribute__((__unused__)) uint32_t interface)
{
    uint32_t keyboardNumber;
    struct HidDevice *hidData;
    struct KeyboardDevice *data;
    struct HidParserResult *parse;

    if ((KeyboardMaxKeyboards & 3) != 0)
    {
        printf("KBD: Warning! KeyboardMaxKeyboards not a multiple of 4. The driver wasn't built for this!\n");
    }
    if (keyboardCount == KeyboardMaxKeyboards)
    {
        printf("KBD: %s not connected. Too many keyboards connected (%d/%d). Change KeyboardMaxKeyboards in device.keyboard.c to allow more.\n", UsbGetDescription(device), keyboardCount, KeyboardMaxKeyboards);
        return ErrorIncompatible;
    }

    hidData = (struct HidDevice *)device->DriverData;
    if (hidData->Header.DeviceDriver != DeviceDriverHid)
    {
        printf("KBD: %s isn't a HID device. The keyboard driver is built upon the HID driver.\n", UsbGetDescription(device));
        return ErrorIncompatible;
    }

    parse = hidData->ParserResult;
    if ((parse->Application.Page != GenericDesktopControl && parse->Application.Page != Undefined) ||
        parse->Application.Desktop != DesktopKeyboard)
    {
        printf("KBD: %s doesn't seem to be a keyboard (%x != %x || %x != %x)...\n", UsbGetDescription(device), parse->Application.Page, GenericDesktopControl, parse->Application.Desktop, DesktopKeyboard);
        return ErrorIncompatible;
    }
    if (parse->ReportCount < 1)
    {
        printf("KBD: %s doesn't have enough outputs to be a keyboard.\n", UsbGetDescription(device));
        return ErrorIncompatible;
    }
    hidData->HidDetached = KeyboardDetached;
    hidData->HidDeallocate = KeyboardDeallocate;
    if ((hidData->DriverData = MemoryAllocate(sizeof(struct KeyboardDevice))) == NULL)
    {
        printf("KBD: Not enough memory to allocate keyboard %s.\n", UsbGetDescription(device));
        return ErrorMemory;
    }
    data = (struct KeyboardDevice *)hidData->DriverData;
    data->Header.DeviceDriver = DeviceDriverKeyboard;
    data->Header.DataSize = sizeof(struct KeyboardDevice);
    data->Index = keyboardNumber = 0xffffffff;
    for (uint32_t i = 0; i < KeyboardMaxKeyboards; i++)
    {
        if (keyboardAddresses[i] == 0)
        {
            data->Index = keyboardNumber = i;
            keyboardAddresses[i] = device->Number;
            keyboardCount++;
            break;
        }
    }

    if (keyboardNumber == 0xffffffff)
    {
        printf("KBD: PANIC! Driver in inconsistent state! KeyboardCount is inaccurate.\n");
        KeyboardDeallocate(device);
        return ErrorGeneral;
    }

    keyboards[keyboardNumber] = device;
    for (uint32_t i = 0; i < KeyboardMaxKeys; i++)
        data->Keys[i] = 0;
    *(uint8_t *)&data->Modifiers = 0;
    *(uint8_t *)&data->LedSupport = 0;

    for (uint32_t i = 0; i < 9; i++)
        data->KeyFields[i] = NULL;
    for (uint32_t i = 0; i < 8; i++)
        data->LedFields[i] = NULL;
    data->LedReport = NULL;
    data->KeyReport = NULL;

    for (uint32_t i = 0; i < parse->ReportCount; i++)
    {
        printf("KBD: type %x report %d. %d fields.\n", parse->Report[i]->Type, i, parse->Report[i]->FieldCount);
        if (parse->Report[i]->Type == Input &&
            data->KeyReport == NULL)
        {
            printf("KBD: Output report %d. %d fields.\n", i, parse->Report[i]->FieldCount);
            data->KeyReport = parse->Report[i];
            for (uint32_t j = 0; j < parse->Report[i]->FieldCount; j++)
            {
                if (parse->Report[i]->Fields[j].Usage.Page == KeyboardControl || parse->Report[i]->Fields[j].Usage.Page == Undefined)
                {
                    if (parse->Report[i]->Fields[j].Attributes.Variable)
                    {
                        if (parse->Report[i]->Fields[j].Usage.Keyboard >= KeyboardLeftControl && parse->Report[i]->Fields[j].Usage.Keyboard <= KeyboardRightGui)
                            printf("KBD: Modifier %d detected! Offset=%x, size=%x\n", parse->Report[i]->Fields[j].Usage.Keyboard, parse->Report[i]->Fields[j].Offset, parse->Report[i]->Fields[j].Size);
                        data->KeyFields[(uint16_t)parse->Report[i]->Fields[j].Usage.Keyboard - (uint16_t)KeyboardLeftControl] =
                            &parse->Report[i]->Fields[j];
                    }
                    else
                    {
                        printf("KBD: Key input detected!\n");
                        data->KeyFields[8] = &parse->Report[i]->Fields[j];
                    }
                }
            }
        }
        else if (parse->Report[i]->Type == Output &&
                 data->LedReport == NULL)
        {
            data->LedReport = parse->Report[i];
            printf("KBD: Input report %d. %d fields.\n", i, parse->Report[i]->FieldCount);
            for (uint32_t j = 0; j < parse->Report[i]->FieldCount; j++)
            {
                if (parse->Report[i]->Fields[j].Usage.Page == Led)
                {
                    switch (parse->Report[i]->Fields[j].Usage.Led)
                    {
                    case LedNumberLock:
                        printf("KBD: Number lock LED detected!\n");
                        data->LedFields[0] = &parse->Report[i]->Fields[j];
                        data->LedSupport.NumberLock = true;
                        break;
                    case LedCapsLock:
                        printf("KBD: Caps lock LED detected!\n");
                        data->LedFields[1] = &parse->Report[i]->Fields[j];
                        data->LedSupport.CapsLock = true;
                        break;
                    case LedScrollLock:
                        printf("KBD: Scroll lock LED detected!\n");
                        data->LedFields[2] = &parse->Report[i]->Fields[j];
                        data->LedSupport.ScrollLock = true;
                        break;
                    case LedCompose:
                        printf("KBD: Compose LED detected!\n");
                        data->LedFields[3] = &parse->Report[i]->Fields[j];
                        data->LedSupport.Compose = true;
                        break;
                    case LedKana:
                        printf("KBD: Kana LED detected!\n");
                        data->LedFields[4] = &parse->Report[i]->Fields[j];
                        data->LedSupport.Kana = true;
                        break;
                    case LedPower:
                        printf("KBD: Power LED detected!\n");
                        data->LedFields[5] = &parse->Report[i]->Fields[j];
                        data->LedSupport.Power = true;
                        break;
                    case LedShift:
                        printf("KBD: Shift LED detected!\n");
                        data->LedFields[6] = &parse->Report[i]->Fields[j];
                        data->LedSupport.Shift = true;
                        break;
                    case LedMute:
                        printf("KBD: Mute LED detected!\n");
                        data->LedFields[7] = &parse->Report[i]->Fields[j];
                        data->LedSupport.Mute = true;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    printf("KBD: New keyboard assigned %d!\n", device->Number);

    return OK;
}

Result KeyboardSetLeds(uint32_t keyboardAddress, struct KeyboardLeds leds)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return ErrorDisconnected;
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;

    if (data->LedSupport.NumberLock)
        data->LedFields[0]->Value.Bool = leds.NumberLock;
    if (data->LedSupport.CapsLock)
        data->LedFields[1]->Value.Bool = leds.CapsLock;
    if (data->LedSupport.ScrollLock)
        data->LedFields[2]->Value.Bool = leds.ScrollLock;
    if (data->LedSupport.Compose)
        data->LedFields[3]->Value.Bool = leds.Compose;
    if (data->LedSupport.Kana)
        data->LedFields[4]->Value.Bool = leds.Kana;
    if (data->LedSupport.Power)
        data->LedFields[5]->Value.Bool = leds.Power;
    if (data->LedSupport.Shift)
        data->LedFields[6]->Value.Bool = leds.Shift;
    if (data->LedSupport.Mute)
        data->LedFields[7]->Value.Bool = leds.Mute;

    return HidWriteDevice(keyboards[keyboardNumber], data->LedReport->Index);
}

struct KeyboardLeds KeyboardGetLedSupport(uint32_t keyboardAddress)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return (struct KeyboardLeds){};
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    return data->LedSupport;
}

Result KeyboardPoll(uint32_t keyboardAddress)
{
    uint32_t keyboardNumber;
    Result result;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return ErrorDisconnected;
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    if ((result = HidReadDevice(keyboards[keyboardNumber], data->KeyReport->Index)) != OK)
    {
        if (result != ErrorDisconnected)
            printf("KBD: Could not get key report from %s.\n", UsbGetDescription(keyboards[keyboardNumber]));
        return result;
    }

    if (data->KeyFields[0] != NULL)
        data->Modifiers.LeftControl = data->KeyFields[0]->Value.Bool;
    if (data->KeyFields[1] != NULL)
        data->Modifiers.LeftShift = data->KeyFields[1]->Value.Bool;
    if (data->KeyFields[2] != NULL)
        data->Modifiers.LeftAlt = data->KeyFields[2]->Value.Bool;
    if (data->KeyFields[3] != NULL)
        data->Modifiers.LeftGui = data->KeyFields[3]->Value.Bool;
    if (data->KeyFields[4] != NULL)
        data->Modifiers.RightControl = data->KeyFields[4]->Value.Bool;
    if (data->KeyFields[5] != NULL)
        data->Modifiers.RightShift = data->KeyFields[5]->Value.Bool;
    if (data->KeyFields[6] != NULL)
        data->Modifiers.RightAlt = data->KeyFields[6]->Value.Bool;
    if (data->KeyFields[7] != NULL)
        data->Modifiers.RightGui = data->KeyFields[7]->Value.Bool;
    if (data->KeyFields[8] != NULL)
    {
        if (HidGetFieldValue(data->KeyFields[8], 0) != KeyboardErrorRollOver)
        {
            data->KeyCount = 0;
            for (uint32_t i = 0; i < KeyboardMaxKeys && i < data->KeyFields[8]->Count; i++)
            {
                if ((data->Keys[i] = HidGetFieldValue(data->KeyFields[8], i)) + (uint16_t)data->KeyFields[8]->Usage.Keyboard != 0)
                    data->KeyCount++;
            }
        }
    }

    return OK;
}

struct KeyboardModifiers KeyboardGetModifiers(uint32_t keyboardAddress)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return (struct KeyboardModifiers){};
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    return data->Modifiers;
}

uint32_t KeyboardGetKeyDownCount(uint32_t keyboardAddress)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return 0;
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    return data->KeyCount;
}

bool KeyboadGetKeyIsDown(uint32_t keyboardAddress, uint16_t key)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return false;
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    for (uint32_t i = 0; i < data->KeyCount; i++)
        if (data->Keys[i] == key)
            return true;
    return false;
}

uint16_t KeyboardGetKeyDown(uint32_t keyboardAddress, uint32_t index)
{
    uint32_t keyboardNumber;
    struct KeyboardDevice *data;
    uint32_t keyCount = KeyboardGetKeyDownCount(keyboardAddress);

    keyboardNumber = KeyboardIndex(keyboardAddress);
    if (keyboardNumber == 0xffffffff)
        return 0;
    data = (struct KeyboardDevice *)((struct HidDevice *)keyboards[keyboardNumber]->DriverData)->DriverData;
    if (index >= keyCount)
        return 0;
    return data->Keys[index];
}
