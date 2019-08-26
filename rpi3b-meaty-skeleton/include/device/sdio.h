#ifndef _SDIO_H
#define _SDIO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdbool.h>

/*--------------------------------------------------------------------------}
{						   SD CARD COMMAND RECORD						    }
{--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------}
{      EMMC CMDTM register - BCM2835.PDF Manual Section 5 pages 69-70       }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regCMDTM {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            unsigned reserved : 1;						// @0		Write as zero read as don't care
            volatile unsigned TM_BLKCNT_EN : 1;			// @1		Enable the block counter for multiple block transfers
            volatile enum {	TM_NO_COMMAND = 0,			//			no command 
                            TM_CMD12 = 1,				//			command CMD12 
                            TM_CMD23 = 2,				//			command CMD23
                            TM_RESERVED = 3,
                          } TM_AUTO_CMD_EN : 2;			// @2-3		Select the command to be send after completion of a data transfer
            volatile unsigned TM_DAT_DIR : 1;			// @4		Direction of data transfer (0 = host to card , 1 = card to host )
            volatile unsigned TM_MULTI_BLOCK : 1;		// @5		Type of data transfer (0 = single block, 1 = muli block)
            unsigned reserved1 : 10;					// @6-15	Write as zero read as don't care
            volatile enum {	CMD_NO_RESP = 0,			//			no response
                            CMD_136BIT_RESP = 1,		//			136 bits response 
                            CMD_48BIT_RESP = 2,			//			48 bits response 
                            CMD_BUSY48BIT_RESP = 3,		//			48 bits response using busy 
                          } CMD_RSPNS_TYPE : 2;			// @16-17
            unsigned reserved2 : 1;						// @18		Write as zero read as don't care
            volatile unsigned CMD_CRCCHK_EN : 1;		// @19		Check the responses CRC (0=disabled, 1= enabled)
            volatile unsigned CMD_IXCHK_EN : 1;			// @20		Check that response has same index as command (0=disabled, 1= enabled)
            volatile unsigned CMD_ISDATA : 1;			// @21		Command involves data transfer (0=disabled, 1= enabled)
            volatile enum {	CMD_TYPE_NORMAL = 0,		//			normal command
                            CMD_TYPE_SUSPEND = 1,		//			suspend command 
                            CMD_TYPE_RESUME = 2,		//			resume command 
                            CMD_TYPE_ABORT = 3,			//			abort command 
                          } CMD_TYPE : 2;				// @22-23 
            volatile unsigned CMD_INDEX : 6;			// @24-29
            unsigned reserved3 : 2;						// @30-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

typedef struct EMMCCommand
{
	const char cmd_name[16];
	struct regCMDTM code;
	struct __attribute__((__packed__)) {
		unsigned use_rca : 1;										// @0		Command uses rca										
		unsigned reserved : 15;										// @1-15	Write as zero read as don't care
		uint16_t delay;												// @16-31	Delay to apply after command
	};
} EMMCCommand;

typedef enum {
	SD_OK				= 0,							// NO error
	SD_ERROR			= 1,							// General non specific SD error
	SD_TIMEOUT			= 2,							// SD Timeout error
	SD_BUSY				= 3,							// SD Card is busy
	SD_NO_RESP			= 5,							// SD Card did not respond
	SD_ERROR_RESET		= 6,							// SD Card did not reset
	SD_ERROR_CLOCK		= 7,							// SD Card clock change failed
	SD_ERROR_VOLTAGE	= 8,							// SD Card does not support requested voltage
	SD_ERROR_APP_CMD	= 9,							// SD Card app command failed						
	SD_CARD_ABSENT		= 10,							// SD Card not present
	SD_READ_ERROR		= 11,
	SD_MOUNT_FAIL		= 12,
} sd_result;

void print_sdio_info(void);
sd_result initialize_sdio(void);
sd_result sdio_send_command(EMMCCommand* cmd, uint32_t arg, uint32_t *response);
sd_result sdio_data_transfer(uint8_t *buf, uint32_t length, bool write);

#ifdef __cplusplus
}
#endif

#endif