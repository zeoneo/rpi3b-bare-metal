#include <device/wifi.h>
#include <device/wifi_io.h>
#include <device/wifi_sb.h>
#include <device/gpio.h>
#include <kernel/systimer.h>
#include <plibc/stdio.h>
#include <stdint.h>

static void connect_emmc_to_wifi(void);
// static uint32_t enable_sdio();

#define V3_3 1 << 20

void enable_wifi(void)
{

    connect_emmc_to_wifi();
}

// static uint32_t enable_sdio()
// {
//     sdio_cmd(IX_GO_IDLE_STATE, 0);

//     // CMD52 send IO reset command
//     // Func: 00h
//     // Register: 02h
//     sdio_cmd(52, 0b10001000000000000000010010000000);
//     uint32_t ocr = sdio_old_cmd(IX_IO_SEND_OP_COND, 0);

//     uint32_t i = 0;
//     while ((ocr & (1 << 31)) == 0)
//     {
//         if (++i > 5)
//         {
//             printf("ether4330: no response to sdio access: ocr = %x\n", ocr);
//             break;
//         }
//         ocr = sdio_old_cmd(IX_IO_SEND_OP_COND, V3_3);
//     }
//     // printf("Final OCR: %x \n",ocr);
//     uint32_t cmd_resp = sdio_cmd(IX_SEND_REL_ADDR, 0);
//     uint32_t rca = (cmd_resp >> 16) << 16;
//     // printf("SEND RCA : rca :%x \n", rca);

//     cmd_resp = sdio_cmd(IX_CARD_SELECT, rca);
//     // printf("IX_CARD_SELECT : resp :%x \n", cmd_resp);

//     sdio_set(Fn0, Highspeed, 2);
//     sdio_set(Fn0, Busifc, 2); /* bus width 4 */
//     sdio_write(Fn0, Fbr1 + Blksize, 64);
//     sdio_write(Fn0, Fbr1 + Blksize + 1, 64 >> 8);
//     sdio_write(Fn0, Fbr2 + Blksize, 512);
//     sdio_write(Fn0, Fbr2 + Blksize + 1, 512 >> 8);
//     sdio_set(Fn0, Ioenable, 1 << Fn1);
//     sdio_write(Fn0, Intenable, 0);

//     for (i = 0;; i++)
//     {
//         if (i == 10)
//         {
//             printf("ether4330: can't enable SDIO function\n");
//             break;
//         }
//         uint32_t xr = sdio_read(Fn0, Ioready);
//         printf(" resp: %x \n ", xr);
//         if (xr & 1 << Fn1)
//         {
//             printf("Enabled SDIO functions \n");
//             return 0;
//         }
//         MicroDelay(100);
//     }

//     return -1;
// }

// uint32_t sdio_cmd(cmd_index_t cmd_index, uint32_t arg)
// {
//     uint32_t resp[4] = {0};
//     sdio_send_command(cmd_index, arg, &resp[0]);
//     return resp[0];
// }

/*
    Not officially documented: emmc can be connected to different gpio pins
        48-53 (SD card)
        22-27 (P1 header)
        34-39 (wifi - pi3 only)
    using ALT3 function to activate the required routing
 */
static void connect_emmc_to_wifi(void)
{
    /* disconnect emmc from SD card (connect sdhost instead) */
    uint32_t i;

    // Following lines connect to SD card to SD HOST
    for (i = 48; i <= 53; i++)
        select_alt_func(i, Alt0);

    // FOllowing lines connect EMMC controller to wifi
    for (i = 34; i <= 39; i++)
    {
        select_alt_func(i, Alt3);
        if (i == 34)
            disable_pulling(i); // Pull off
        else
            pullup_pin(i);
    }
}