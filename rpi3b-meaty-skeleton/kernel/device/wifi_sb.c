#include <device/wifi_sb.h>
#include <device/wifi_io.h>
#include <device/wifi.h>
#include <plibc/stdio.h>

static void sb_window(uint32_t addr)
{
    addr &= ~(Sbwsize - 1);
    config_write(Sbaddr, addr >> 8);
    config_write(Sbaddr + 1, addr >> 16);
    config_write(Sbaddr + 2, addr >> 24);
}

uint32_t sb_init(struct Ctrl *ctrl)
{
    uint32_t r;
    uint32_t chipid;

    sb_window(Enumbase);
    r = config_readl(Fn1, Enumbase);
    chipid = r & 0xFFFF;
    printf("ether4330: chip %d rev %ld type %ld\n", chipid, (r >> 16) & 0xF, (r >> 28) & 0xF);
    switch (chipid)
    {
        case 0x4330:
    case 43362:
    case 43430:
    case 0x4345:
        ctrl->chip_id = chipid;
        ctrl->chip_rev = (r >> 16) & 0xF;
        break;
    default:
        printf("ether4330: chipid %#x (%d) not supported\n", chipid, chipid);
        return -1;
    }
    r = config_readl(Fn1, Enumbase + 63 * 4);
    return 0;
}