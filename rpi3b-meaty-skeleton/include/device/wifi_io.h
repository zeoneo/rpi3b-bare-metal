#ifndef _WIFI_IO_H_
#define _WIFI_IO_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    uint32_t emmccmd(uint32_t cmd, uint32_t arg, uint32_t *resp);
    void reset_cmd_circuit();
    void sdio_write(uint32_t fn, uint32_t addr, uint32_t data);
    uint32_t sdio_read(uint32_t fn, uint32_t addr);
    void sdio_set(uint32_t fn, uint32_t addr, uint32_t bits);

    void config_write(uint32_t offset, uint32_t value);
    uint32_t config_read(uint32_t offset);
    uint32_t config_readl(uint32_t fn, uint32_t off);
    uint32_t sdio_old_cmd(uint32_t cmd_index, uint32_t arg);
    void sdio_rw_ext(uint32_t fn, uint32_t write, void *a, uint32_t len, uint32_t addr, uint32_t incr);

    // Some of the functions are implemented in sdio-util.c

#ifdef __cplusplus
}
#endif

#endif