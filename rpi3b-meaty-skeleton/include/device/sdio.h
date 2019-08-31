#ifndef _SDIO_H
#define _SDIO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdbool.h>
#include<device/emmc.h>


/*--------------------------------------------------------------------------}
{						   SD CARD COMMAND RECORD						    }
{--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------}
{						SD CARD COMMAND INDEX DEFINITIONS				    }
{--------------------------------------------------------------------------*/

typedef enum {
  IX_GO_IDLE_STATE    = 0,
  IX_ALL_SEND_CID     = 1,
  IX_SEND_REL_ADDR    = 2,
  IX_SET_DSR          = 3,
  IX_SWITCH_FUNC      = 4,
  IX_IO_SEND_OP_COND  = 5,
  IX_CARD_SELECT      = 6,
  IX_SEND_IF_COND     = 7,
  IX_SEND_CSD         = 8,
  IX_SEND_CID         = 9,
  IX_VOLTAGE_SWITCH   = 10,
  IX_STOP_TRANS       = 11,
  IX_SEND_STATUS      = 12,
  IX_GO_INACTIVE      = 13,
  IX_SET_BLOCKLEN     = 14,
  IX_READ_SINGLE      = 15,
  IX_READ_MULTI       = 16,
  IX_SEND_TUNING      = 17,
  IX_SPEED_CLASS      = 18,
  IX_SET_BLOCKCNT     = 19,
  IX_WRITE_SINGLE     = 20,
  IX_WRITE_MULTI      = 21,
  IX_PROGRAM_CSD      = 22,
  IX_SET_WRITE_PR     = 23,
  IX_CLR_WRITE_PR     = 24,
  IX_SND_WRITE_PR     = 25,
  IX_ERASE_WR_ST      = 26,
  IX_ERASE_WR_END     = 27,
  IX_ERASE            = 28,
  IX_LOCK_UNLOCK      = 29,
  IX_APP_CMD          = 30,
  IX_APP_CMD_RCA      = 31,
  IX_GEN_CMD          = 32,

// Commands hereafter require APP_CMD.
  IX_SET_BUS_WIDTH    = 33,
  IX_SD_STATUS        = 34,
  IX_SEND_NUM_WRBL    = 35,
  IX_SEND_NUM_ERS     = 36,
  IX_APP_SEND_OP_COND = 37,
  IX_SET_CLR_DET      = 38,
  IX_SEND_SCR         = 39
} cmd_index_t;


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

void print_sdio_info(void);
SDRESULT initialize_sdio(void);
SDRESULT sdio_send_command(cmd_index_t cmd_index, uint32_t arg, uint32_t *response);
SDRESULT sdio_data_transfer(uint8_t *buf, uint32_t length, bool write);
SDRESULT sd_send_command ( int index );


#ifdef __cplusplus
}
#endif

#endif