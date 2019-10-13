#ifndef _WIFI_SB_H_
#define _WIFI_SB_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    /* Sonics Silicon Backplane (access to cores on chip) */
    enum
    {
        Sbwsize = 0x8000,
        Sb32bit = 0x8000,
        Sbaddr = 0x1000a,
        Enumbase = 0x18000000,
        Framectl = 0x1000d,
        Rfhalt = 0x01,
        Wfhalt = 0x02,
        Clkcsr = 0x1000e,
        ForceALP = 0x01, /* active low-power clock */
        ForceHT = 0x02,  /* high throughput clock */
        ForceILP = 0x04, /* idle low-power clock */
        ReqALP = 0x08,
        ReqHT = 0x10,
        Nohwreq = 0x20,
        ALPavail = 0x40,
        HTavail = 0x80,
        Pullups = 0x1000f,
        Wfrmcnt = 0x10019,
        Rfrmcnt = 0x1001b,
    };

    uint32_t sb_init();

#ifdef __cplusplus
}
#endif

#endif