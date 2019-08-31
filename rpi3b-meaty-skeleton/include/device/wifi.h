#ifndef _WIFI_H_
#define _WIFI_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include<device/sdio.h>

void enable_wifi(void);
uint32_t sdio_cmd(cmd_index_t cmd_index, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif