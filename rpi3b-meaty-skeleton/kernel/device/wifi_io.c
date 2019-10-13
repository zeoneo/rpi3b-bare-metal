#include <device/wifi_io.h>
#include <device/wifi.h>
#include <device/wifi_sb.h>
#include <string.h>

#define CACHELINESZ 64 /* temp */

void config_write(uint32_t offset, uint32_t value)
{
    sdio_write(Fn1, offset, value);
}

uint32_t config_read(uint32_t offset)
{
    return sdio_read(Fn1, offset);
}

uint32_t config_readl(uint32_t fn, uint32_t off)
{
    uint8_t cbuf[2 * CACHELINESZ];
    uint8_t *p;

    p = (uint8_t *)ROUND((uint32_t)&cbuf[0], CACHELINESZ);
    memset(p, 0, 4);
    sdio_rw_ext(fn, 0, p, 4, off | Sb32bit, 1);
    // if (SDIODEBUG)
    //     print("cfgreadl %lux: %2.2x %2.2x %2.2x %2.2x\n", off, p[0], p[1], p[2], p[3]);
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}