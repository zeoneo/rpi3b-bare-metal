#include<kernel/rpi-base.h>
#include<kernel/systimer.h>
#include<device/sdio.h>
#include<plibc/stdio.h>

#include<stdint.h>

#define DEBUG_INFO 1

#if DEBUG_INFO == 1
#define LOG_DEBUG(...) printf( __VA_ARGS__ )
#else
#define LOG_DEBUG(...)
#endif

#define	USED(x) if(x);else

// Following register declarations are COPIED from Ldb-Ecm github Repository.

/*--------------------------------------------------------------------------}
{      EMMC BLKSIZECNT register - BCM2835.PDF Manual Section 5 page 68      }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regBLKSIZECNT {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned BLKSIZE : 10;				// @0-9		Block size in bytes
            unsigned reserved : 6;						// @10-15	Write as zero read as don't care
            volatile unsigned BLKCNT : 16;				// @16-31	Number of blocks to be transferred 
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};


/*--------------------------------------------------------------------------}
{        EMMC STATUS register - BCM2835.PDF Manual Section 5 page 72        }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regSTATUS {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned CMD_INHIBIT : 1;			// @0		Command line still used by previous command
            volatile unsigned DAT_INHIBIT : 1;			// @1		Data lines still used by previous data transfer
            volatile unsigned DAT_ACTIVE : 1;			// @2		At least one data line is active
            unsigned reserved : 5;						// @3-7		Write as zero read as don't care
            volatile unsigned WRITE_TRANSFER : 1;		// @8		New data can be written to EMMC
            volatile unsigned READ_TRANSFER : 1;		// @9		New data can be read from EMMC
            unsigned reserved1 : 10;					// @10-19	Write as zero read as don't care
            volatile unsigned DAT_LEVEL0 : 4;			// @20-23	Value of data lines DAT3 to DAT0
            volatile unsigned CMD_LEVEL : 1;			// @24		Value of command line CMD 
            volatile unsigned DAT_LEVEL1 : 4;			// @25-28	Value of data lines DAT7 to DAT4
            unsigned reserved2 : 3;						// @29-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{      EMMC CONTROL0 register - BCM2835.PDF Manual Section 5 page 73/74     }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regCONTROL0 {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            unsigned reserved : 1;						// @0		Write as zero read as don't care
            volatile unsigned HCTL_DWIDTH : 1;			// @1		Use 4 data lines (true = enable)
            volatile unsigned HCTL_HS_EN : 1;			// @2		Select high speed mode (true = enable)
            unsigned reserved1 : 2;						// @3-4		Write as zero read as don't care
            volatile unsigned HCTL_8BIT : 1;			// @5		Use 8 data lines (true = enable)
            unsigned reserved2 : 10;					// @6-15	Write as zero read as don't care
            volatile unsigned GAP_STOP : 1;				// @16		Stop the current transaction at the next block gap
            volatile unsigned GAP_RESTART : 1;			// @17		Restart a transaction last stopped using the GAP_STOP
            volatile unsigned READWAIT_EN : 1;			// @18		Use DAT2 read-wait protocol for cards supporting this
            volatile unsigned GAP_IEN : 1;				// @19		Enable SDIO interrupt at block gap 
            volatile unsigned SPI_MODE : 1;				// @20		SPI mode enable
            volatile unsigned BOOT_EN : 1;				// @21		Boot mode access
            volatile unsigned ALT_BOOT_EN : 1;			// @22		Enable alternate boot mode access
            unsigned reserved3 : 9;						// @23-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{      EMMC CONTROL1 register - BCM2835.PDF Manual Section 5 page 74/75     }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regCONTROL1 {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned CLK_INTLEN : 1;			// @0		Clock enable for internal EMMC clocks for power saving
            volatile const unsigned CLK_STABLE : 1;		// @1		SD clock stable  0=No 1=yes   **read only
            volatile unsigned CLK_EN : 1;				// @2		SD clock enable  0=disable 1=enable
            unsigned reserved : 2;						// @3-4		Write as zero read as don't care
            volatile unsigned CLK_GENSEL : 1;			// @5		Mode of clock generation (0=Divided, 1=Programmable)
            volatile unsigned CLK_FREQ_MS2 : 2;			// @6-7		SD clock base divider MSBs (Version3+ only)
            volatile unsigned CLK_FREQ8 : 8;			// @8-15	SD clock base divider LSBs
            volatile unsigned DATA_TOUNIT : 4;			// @16-19	Data timeout unit exponent
            unsigned reserved1 : 4;						// @20-23	Write as zero read as don't care
            volatile unsigned SRST_HC : 1;				// @24		Reset the complete host circuit
            volatile unsigned SRST_CMD : 1;				// @25		Reset the command handling circuit
            volatile unsigned SRST_DATA : 1;			// @26		Reset the data handling circuit
            unsigned reserved2 : 5;						// @27-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{    EMMC CONTROL2 register - BCM2835.PDF Manual Section 5 pages 81-84      }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regCONTROL2 {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile const unsigned ACNOX_ERR : 1;		// @0		Auto command not executed due to an error **read only
            volatile const unsigned ACTO_ERR : 1;		// @1		Timeout occurred during auto command execution **read only
            volatile const unsigned ACCRC_ERR : 1;		// @2		Command CRC error occurred during auto command execution **read only
            volatile const unsigned ACEND_ERR : 1;		// @3		End bit is not 1 during auto command execution **read only
            volatile const unsigned ACBAD_ERR : 1;		// @4		Command index error occurred during auto command execution **read only
            unsigned reserved : 2;						// @5-6		Write as zero read as don't care
            volatile const unsigned NOTC12_ERR : 1;		// @7		Error occurred during auto command CMD12 execution **read only
            unsigned reserved1 : 8;						// @8-15	Write as zero read as don't care			
            volatile enum { SDR12 = 0,
                            SDR25 = 1, 
                            SDR50 = 2,
                            SDR104 = 3,
                            DDR50 = 4,
                          } UHSMODE : 3;				// @16-18	Select the speed mode of the SD card (SDR12, SDR25 etc)
            unsigned reserved2 : 3;						// @19-21	Write as zero read as don't care
            volatile unsigned TUNEON : 1;				// @22		Start tuning the SD clock
            volatile unsigned TUNED : 1;				// @23		Tuned clock is used for sampling data
            unsigned reserved3 : 8;						// @24-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{     EMMC INTERRUPT register - BCM2835.PDF Manual Section 5 pages 75-77    }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regINTERRUPT {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned CMD_DONE : 1;				// @0		Command has finished
            volatile unsigned DATA_DONE : 1;			// @1		Data transfer has finished
            volatile unsigned BLOCK_GAP : 1;			// @2		Data transfer has stopped at block gap
            unsigned reserved : 1;						// @3		Write as zero read as don't care
            volatile unsigned WRITE_RDY : 1;			// @4		Data can be written to DATA register
            volatile unsigned READ_RDY : 1;				// @5		DATA register contains data to be read
            unsigned reserved1 : 2;						// @6-7		Write as zero read as don't care
            volatile unsigned CARD : 1;					// @8		Card made interrupt request
            unsigned reserved2 : 3;						// @9-11	Write as zero read as don't care
            volatile unsigned RETUNE : 1;				// @12		Clock retune request was made
            volatile unsigned BOOTACK : 1;				// @13		Boot acknowledge has been received
            volatile unsigned ENDBOOT : 1;				// @14		Boot operation has terminated
            volatile unsigned ERR : 1;					// @15		An error has occured
            volatile unsigned CTO_ERR : 1;				// @16		Timeout on command line
            volatile unsigned CCRC_ERR : 1;				// @17		Command CRC error
            volatile unsigned CEND_ERR : 1;				// @18		End bit on command line not 1
            volatile unsigned CBAD_ERR : 1;				// @19		Incorrect command index in response
            volatile unsigned DTO_ERR : 1;				// @20		Timeout on data line
            volatile unsigned DCRC_ERR : 1;				// @21		Data CRC error
            volatile unsigned DEND_ERR : 1;				// @22		End bit on data line not 1
            unsigned reserved3 : 1;						// @23		Write as zero read as don't care
            volatile unsigned ACMD_ERR : 1;				// @24		Auto command error
            unsigned reserved4 : 7;						// @25-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{     EMMC IRPT_MASK register - BCM2835.PDF Manual Section 5 pages 77-79    }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regIRPT_MASK {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned CMD_DONE : 1;				// @0		Command has finished
            volatile unsigned DATA_DONE : 1;			// @1		Data transfer has finished
            volatile unsigned BLOCK_GAP : 1;			// @2		Data transfer has stopped at block gap
            unsigned reserved : 1;						// @3		Write as zero read as don't care
            volatile unsigned WRITE_RDY : 1;			// @4		Data can be written to DATA register
            volatile unsigned READ_RDY : 1;				// @5		DATA register contains data to be read
            unsigned reserved1 : 2;						// @6-7		Write as zero read as don't care
            volatile unsigned CARD : 1;					// @8		Card made interrupt request
            unsigned reserved2 : 3;						// @9-11	Write as zero read as don't care
            volatile unsigned RETUNE : 1;				// @12		Clock retune request was made
            volatile unsigned BOOTACK : 1;				// @13		Boot acknowledge has been received
            volatile unsigned ENDBOOT : 1;				// @14		Boot operation has terminated
            volatile unsigned ERR : 1;					// @15		An error has occured
            volatile unsigned CTO_ERR : 1;				// @16		Timeout on command line
            volatile unsigned CCRC_ERR : 1;				// @17		Command CRC error
            volatile unsigned CEND_ERR : 1;				// @18		End bit on command line not 1
            volatile unsigned CBAD_ERR : 1;				// @19		Incorrect command index in response
            volatile unsigned DTO_ERR : 1;				// @20		Timeout on data line
            volatile unsigned DCRC_ERR : 1;				// @21		Data CRC error
            volatile unsigned DEND_ERR : 1;				// @22		End bit on data line not 1
            unsigned reserved3 : 1;						// @23		Write as zero read as don't care
            volatile unsigned ACMD_ERR : 1;				// @24		Auto command error
            unsigned reserved4 : 7;						// @25-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{      EMMC IRPT_EN register - BCM2835.PDF Manual Section 5 pages 79-71     }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regIRPT_EN {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned CMD_DONE : 1;				// @0		Command has finished
            volatile unsigned DATA_DONE : 1;			// @1		Data transfer has finished
            volatile unsigned BLOCK_GAP : 1;			// @2		Data transfer has stopped at block gap
            unsigned reserved : 1;						// @3		Write as zero read as don't care
            volatile unsigned WRITE_RDY : 1;			// @4		Data can be written to DATA register
            volatile unsigned READ_RDY : 1;				// @5		DATA register contains data to be read
            unsigned reserved1 : 2;						// @6-7		Write as zero read as don't care
            volatile unsigned CARD : 1;					// @8		Card made interrupt request
            unsigned reserved2 : 3;						// @9-11	Write as zero read as don't care
            volatile unsigned RETUNE : 1;				// @12		Clock retune request was made
            volatile unsigned BOOTACK : 1;				// @13		Boot acknowledge has been received
            volatile unsigned ENDBOOT : 1;				// @14		Boot operation has terminated
            volatile unsigned ERR : 1;					// @15		An error has occured
            volatile unsigned CTO_ERR : 1;				// @16		Timeout on command line
            volatile unsigned CCRC_ERR : 1;				// @17		Command CRC error
            volatile unsigned CEND_ERR : 1;				// @18		End bit on command line not 1
            volatile unsigned CBAD_ERR : 1;				// @19		Incorrect command index in response
            volatile unsigned DTO_ERR : 1;				// @20		Timeout on data line
            volatile unsigned DCRC_ERR : 1;				// @21		Data CRC error
            volatile unsigned DEND_ERR : 1;				// @22		End bit on data line not 1
            unsigned reserved3 : 1;						// @23		Write as zero read as don't care
            volatile unsigned ACMD_ERR : 1;				// @24		Auto command error
            unsigned reserved4 : 7;						// @25-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{       EMMC TUNE_STEP  register - BCM2835.PDF Manual Section 5 page 86     }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regTUNE_STEP {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile enum {	TUNE_DELAY_200ps  = 0,
                            TUNE_DELAY_400ps  = 1,
                            TUNE_DELAY_400psA = 2,		// I dont understand the duplicate value???
                            TUNE_DELAY_600ps  = 3,
                            TUNE_DELAY_700ps  = 4,
                            TUNE_DELAY_900ps  = 5,
                            TUNE_DELAY_900psA = 6,		// I dont understand the duplicate value??
                            TUNE_DELAY_1100ps = 7,
                           } DELAY : 3;					// @0-2		Select the speed mode of the SD card (SDR12, SDR25 etc)
            unsigned reserved : 29;						// @3-31	Write as zero read as don't care
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/*--------------------------------------------------------------------------}
{    EMMC SLOTISR_VER register - BCM2835.PDF Manual Section 5 pages 87-88   }
{--------------------------------------------------------------------------*/
struct __attribute__((__packed__, aligned(4))) regSLOTISR_VER {
    union {
        struct __attribute__((__packed__, aligned(1))) {
            volatile unsigned SLOT_STATUS : 8;			// @0-7		Logical OR of interrupt and wakeup signal for each slot
            unsigned reserved : 8;						// @8-15	Write as zero read as don't care
            volatile unsigned SDVERSION : 8;			// @16-23	Host Controller specification version 
            volatile unsigned VENDOR : 8;				// @24-31	Vendor Version Number 
        };
        volatile uint32_t Raw32;						// @0-31	Union to access all 32 bits as a uint32_t
    };
};

/***************************************************************************}
{         PRIVATE POINTERS TO ALL THE BCM2835 EMMC HOST REGISTERS           }
****************************************************************************/
#define EMMC_ARG2			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300000))
#define EMMC_BLKSIZECNT		((volatile struct __attribute__((aligned(4))) regBLKSIZECNT*)(uintptr_t)(PERIPHERAL_BASE + 0x300004))
#define EMMC_ARG1			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300008))
#define EMMC_CMDTM			((volatile struct __attribute__((aligned(4))) regCMDTM*)(uintptr_t)(PERIPHERAL_BASE + 0x30000c))
#define EMMC_RESP0			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300010))
#define EMMC_RESP1			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300014))
#define EMMC_RESP2			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300018))
#define EMMC_RESP3			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x30001C))
#define EMMC_DATA			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300020))
#define EMMC_STATUS			((volatile struct __attribute__((aligned(4))) regSTATUS*)(uintptr_t)(PERIPHERAL_BASE + 0x300024))
#define EMMC_CONTROL0		((volatile struct __attribute__((aligned(4))) regCONTROL0*)(uintptr_t)(PERIPHERAL_BASE + 0x300028))
#define EMMC_CONTROL1		((volatile struct __attribute__((aligned(4))) regCONTROL1*)(uintptr_t)(PERIPHERAL_BASE + 0x30002C))
#define EMMC_INTERRUPT		((volatile struct __attribute__((aligned(4))) regINTERRUPT*)(uintptr_t)(PERIPHERAL_BASE + 0x300030))
#define EMMC_IRPT_MASK		((volatile struct __attribute__((aligned(4))) regIRPT_MASK*)(uintptr_t)(PERIPHERAL_BASE + 0x300034))
#define EMMC_IRPT_EN		((volatile struct __attribute__((aligned(4))) regIRPT_EN*)(uintptr_t)(PERIPHERAL_BASE + 0x300038))
#define EMMC_CONTROL2		((volatile struct __attribute__((aligned(4))) regCONTROL2*)(uintptr_t)(PERIPHERAL_BASE + 0x30003C))
#define EMMC_TUNE_STEP 		((volatile struct __attribute__((aligned(4))) regTUNE_STEP*)(uintptr_t)(PERIPHERAL_BASE + 0x300088))
#define EMMC_SLOTISR_VER	((volatile struct __attribute__((aligned(4))) regSLOTISR_VER*)(uintptr_t)(PERIPHERAL_BASE + 0x3000fC))

/*--------------------------------------------------------------------------}
{			  INTERRUPT REGISTER TURN TO MASK BIT DEFINITIONS			    }
{--------------------------------------------------------------------------*/
#define INT_AUTO_ERROR   0x01000000									// ACMD_ERR bit in register
#define INT_DATA_END_ERR 0x00400000									// DEND_ERR bit in register
#define INT_DATA_CRC_ERR 0x00200000									// DCRC_ERR bit in register
#define INT_DATA_TIMEOUT 0x00100000									// DTO_ERR bit in register
#define INT_INDEX_ERROR  0x00080000									// CBAD_ERR bit in register
#define INT_END_ERROR    0x00040000									// CEND_ERR bit in register
#define INT_CRC_ERROR    0x00020000									// CCRC_ERR bit in register
#define INT_CMD_TIMEOUT  0x00010000									// CTO_ERR bit in register
#define INT_ERR          0x00008000									// ERR bit in register
#define INT_ENDBOOT      0x00004000									// ENDBOOT bit in register
#define INT_BOOTACK      0x00002000									// BOOTACK bit in register
#define INT_RETUNE       0x00001000									// RETUNE bit in register
#define INT_CARD         0x00000100									// CARD bit in register
#define INT_READ_RDY     0x00000020									// READ_RDY bit in register
#define INT_WRITE_RDY    0x00000010									// WRITE_RDY bit in register
#define INT_BLOCK_GAP    0x00000004									// BLOCK_GAP bit in register
#define INT_DATA_DONE    0x00000002									// DATA_DONE bit in register
#define INT_CMD_DONE     0x00000001									// CMD_DONE bit in register
#define INT_ERROR_MASK   (INT_CRC_ERROR|INT_END_ERROR|INT_INDEX_ERROR| \
                          INT_DATA_TIMEOUT|INT_DATA_CRC_ERR|INT_DATA_END_ERR| \
                          INT_ERR|INT_AUTO_ERROR)
#define INT_ALL_MASK     (INT_CMD_DONE|INT_DATA_DONE|INT_READ_RDY|INT_WRITE_RDY|INT_ERROR_MASK)

/*--------------------------------------------------------------------------}
{						  SD CARD FREQUENCIES							    }
{--------------------------------------------------------------------------*/
#define FREQ_SETUP				400000  // 400 Khz
#define FREQ_NORMAL			  25000000  // 25 Mhz
#define FREQ_EXT			  100000000  // 100 Mhz

/*--------------------------------------------------------------------------}
{						  CMD 41 BIT SELECTIONS							    }
{--------------------------------------------------------------------------*/
#define ACMD41_HCS           0x40000000
#define ACMD41_SDXC_POWER    0x10000000
#define ACMD41_S18R          0x04000000
#define ACMD41_VOLTAGE       0x00ff8000
/* PI DOES NOT SUPPORT VOLTAGE SWITCH */
#define ACMD41_ARG_HC        (ACMD41_HCS|ACMD41_SDXC_POWER|ACMD41_VOLTAGE)//(ACMD41_HCS|ACMD41_SDXC_POWER|ACMD41_VOLTAGE|ACMD41_S18R)
#define ACMD41_ARG_SC        (ACMD41_VOLTAGE)   //(ACMD41_VOLTAGE|ACMD41_S18R)


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

#define IX_APP_CMD_START 32  // Used to detect which command needs app command

/*--------------------------------------------------------------------------}
{							  SD CARD COMMAND TABLE						    }
{--------------------------------------------------------------------------*/
static EMMCCommand sdCommandTable[IX_SEND_SCR + 1] =  {
	[IX_GO_IDLE_STATE] =	{ "GO_IDLE_STATE", .code.CMD_INDEX = 0x00, .code.CMD_RSPNS_TYPE = CMD_NO_RESP        , .use_rca = 0 , .delay = 0},
	[IX_ALL_SEND_CID] =		{ "ALL_SEND_CID" , .code.CMD_INDEX = 0x02, .code.CMD_RSPNS_TYPE = CMD_136BIT_RESP    , .use_rca = 0 , .delay = 0},
	[IX_SEND_REL_ADDR] =	{ "SEND_REL_ADDR", .code.CMD_INDEX = 0x03, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SET_DSR] =			{ "SET_DSR"      , .code.CMD_INDEX = 0x04, .code.CMD_RSPNS_TYPE = CMD_NO_RESP        , .use_rca = 0 , .delay = 0},
    [IX_IO_SEND_OP_COND] =	{ "IO_SEND_OP_COND", .code.CMD_INDEX = 0x05, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP   , .use_rca = 0 , .delay = 0},
	[IX_SWITCH_FUNC] =		{ "SWITCH_FUNC"  , .code.CMD_INDEX = 0x06, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_CARD_SELECT] =		{ "CARD_SELECT"  , .code.CMD_INDEX = 0x07, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 1 , .delay = 0},
	[IX_SEND_IF_COND] = 	{ "SEND_IF_COND" , .code.CMD_INDEX = 0x08, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 100},
	[IX_SEND_CSD] =			{ "SEND_CSD"     , .code.CMD_INDEX = 0x09, .code.CMD_RSPNS_TYPE = CMD_136BIT_RESP    , .use_rca = 1 , .delay = 0},
	[IX_SEND_CID] =			{ "SEND_CID"     , .code.CMD_INDEX = 0x0A, .code.CMD_RSPNS_TYPE = CMD_136BIT_RESP    , .use_rca = 1 , .delay = 0},
	[IX_VOLTAGE_SWITCH] =	{ "VOLT_SWITCH"  , .code.CMD_INDEX = 0x0B, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_STOP_TRANS] =		{ "STOP_TRANS"   , .code.CMD_INDEX = 0x0C, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 0 , .delay = 0},
	[IX_SEND_STATUS] =		{ "SEND_STATUS"  , .code.CMD_INDEX = 0x0D, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 1 , .delay = 0},
	[IX_GO_INACTIVE] =		{ "GO_INACTIVE"  , .code.CMD_INDEX = 0x0F, .code.CMD_RSPNS_TYPE = CMD_NO_RESP        , .use_rca = 1 , .delay = 0},
	[IX_SET_BLOCKLEN] =		{ "SET_BLOCKLEN" , .code.CMD_INDEX = 0x10, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_READ_SINGLE] =		{ "READ_SINGLE"  , .code.CMD_INDEX = 0x11, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , 
	                                           .code.CMD_ISDATA = 1  , .code.TM_DAT_DIR = 1,					   .use_rca = 0 , .delay = 0},
	[IX_READ_MULTI] =		{ "READ_MULTI"   , .code.CMD_INDEX = 0x12, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , 
											   .code.CMD_ISDATA = 1 ,  .code.TM_DAT_DIR = 1,
											   .code.TM_BLKCNT_EN =1 , .code.TM_MULTI_BLOCK = 1,                   .use_rca = 0 , .delay = 0},
	[IX_SEND_TUNING] =		{ "SEND_TUNING"  , .code.CMD_INDEX = 0x13, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SPEED_CLASS] =		{ "SPEED_CLASS"  , .code.CMD_INDEX = 0x14, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 0 , .delay = 0},
	[IX_SET_BLOCKCNT] =		{ "SET_BLOCKCNT" , .code.CMD_INDEX = 0x17, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_WRITE_SINGLE] =		{ "WRITE_SINGLE" , .code.CMD_INDEX = 0x18, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , 
											   .code.CMD_ISDATA = 1  ,											   .use_rca = 0 , .delay = 0},
	[IX_WRITE_MULTI] =		{ "WRITE_MULTI"  , .code.CMD_INDEX = 0x19, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , 
											   .code.CMD_ISDATA = 1  ,  
											   .code.TM_BLKCNT_EN = 1, .code.TM_MULTI_BLOCK = 1,				   .use_rca = 0 , .delay = 0},
	[IX_PROGRAM_CSD] =		{ "PROGRAM_CSD"  , .code.CMD_INDEX = 0x1B, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SET_WRITE_PR] =		{ "SET_WRITE_PR" , .code.CMD_INDEX = 0x1C, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 0 , .delay = 0},
	[IX_CLR_WRITE_PR] =		{ "CLR_WRITE_PR" , .code.CMD_INDEX = 0x1D, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 0 , .delay = 0},
	[IX_SND_WRITE_PR] =		{ "SND_WRITE_PR" , .code.CMD_INDEX = 0x1E, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_ERASE_WR_ST] =		{ "ERASE_WR_ST"  , .code.CMD_INDEX = 0x20, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_ERASE_WR_END] =		{ "ERASE_WR_END" , .code.CMD_INDEX = 0x21, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_ERASE] =			{ "ERASE"        , .code.CMD_INDEX = 0x26, .code.CMD_RSPNS_TYPE = CMD_BUSY48BIT_RESP , .use_rca = 0 , .delay = 0},
	[IX_LOCK_UNLOCK] =		{ "LOCK_UNLOCK"  , .code.CMD_INDEX = 0x2A, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_APP_CMD] =			{ "APP_CMD"      , .code.CMD_INDEX = 0x37, .code.CMD_RSPNS_TYPE = CMD_NO_RESP        , .use_rca = 0 , .delay = 100},
	[IX_APP_CMD_RCA] =		{ "APP_CMD"      , .code.CMD_INDEX = 0x37, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 1 , .delay = 0},
	[IX_GEN_CMD] =			{ "GEN_CMD"      , .code.CMD_INDEX = 0x38, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},

	// APP commands must be prefixed by an APP_CMD.
	[IX_SET_BUS_WIDTH] =	{ "SET_BUS_WIDTH", .code.CMD_INDEX = 0x06, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SD_STATUS] =		{ "SD_STATUS"    , .code.CMD_INDEX = 0x0D, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 1 , .delay = 0},
	[IX_SEND_NUM_WRBL] =	{ "SEND_NUM_WRBL", .code.CMD_INDEX = 0x16, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SEND_NUM_ERS] =		{ "SEND_NUM_ERS" , .code.CMD_INDEX = 0x17, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_APP_SEND_OP_COND] =	{ "SD_SENDOPCOND", .code.CMD_INDEX = 0x29, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 1000},
	[IX_SET_CLR_DET] =		{ "SET_CLR_DET"  , .code.CMD_INDEX = 0x2A, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , .use_rca = 0 , .delay = 0},
	[IX_SEND_SCR] =			{ "SEND_SCR"     , .code.CMD_INDEX = 0x33, .code.CMD_RSPNS_TYPE = CMD_48BIT_RESP     , 
											   .code.CMD_ISDATA = 1  , .code.TM_DAT_DIR = 1,					   .use_rca = 0 , .delay = 0},
};

typedef struct Ctlr Ctlr;

struct Ctlr {
    uint32_t    datadone;
    uint32_t    fastclock;
    uint64_t    extclk;
};

#define Mhz 1000000

enum {
    DTO		= 14,		/* data timeout exponent (guesswork) */
    MMCSelect	= 7,		/* mmc/sd card select command */
    Setbuswidth	= 6,		/* mmc/sd set bus width command */
};


typedef struct SDDescriptor {
	uint64_t CardCapacity;						// Card capacity expanded .. calculated from card details
	uint32_t rca;								// Card rca
	uint32_t status;							// Card last status
	EMMCCommand* lastCmd;
} SDDescriptor;

/*--------------------------------------------------------------------------}
{					    CURRENT SD CARD DATA STORAGE					    }
{--------------------------------------------------------------------------*/
static SDDescriptor sdCard = { 0 };
static Ctlr emmc = {0};


static uint_fast8_t fls_uint32_t (uint32_t x) 
{
    uint_fast8_t r = 32;											// Start at 32
    if (!x)  return 0;												// If x is zero answer must be zero
    if (!(x & 0xffff0000u)) {										// If none of the upper word bits are set
        x <<= 16;													// We can roll it up 16 bits
        r -= 16;													// Reduce r by 16
    }
    if (!(x & 0xff000000u)) {										// If none of uppermost byte bits are set
        x <<= 8;													// We can roll it up by 8 bits
        r -= 8;														// Reduce r by 8
    }
    if (!(x & 0xf0000000u)) {										// If none of the uppermost 4 bits are set
        x <<= 4;													// We can roll it up by 4 bits
        r -= 4;														// Reduce r by 4
    }
    if (!(x & 0xc0000000u)) {										// If none of the uppermost 2 bits are set
        x <<= 2;													// We can roll it up by 2 bits
        r -= 2;														// Reduce r by 2
    }
    if (!(x & 0x80000000u)) {										// If the uppermost bit is not set
        x <<= 1;													// We can roll it up by 1 bit
        r -= 1;														// Reduce r by 1
    }
    return r;														// Return the number of the uppermost set bit
}

static uint32_t sdGetClockDivider (uint32_t freq) 
{
    uint32_t divisor = (41666667 + freq - 1) / freq;				// Pi SD frequency is always 41.66667Mhz on baremetal
    if (divisor > 0x3FF) divisor = 0x3FF;							// Constrain divisor to max 0x3FF
    if (EMMC_SLOTISR_VER->SDVERSION < 2) {							// Any version less than HOST SPECIFICATION 3 (Aka numeric 2)						
        uint_fast8_t shiftcount = fls_uint32_t(divisor);			// Only 8 bits and set pwr2 div on Hosts specs 1 & 2
        if (shiftcount > 0) shiftcount--;							// Note the offset of shift by 1 (look at the spec)
        if (shiftcount > 7) shiftcount = 7;							// It's only 8 bits maximum on HOST_SPEC_V2
        divisor = ((uint32_t)1 << shiftcount);						// Version 1,2 take power 2
    } else if (divisor < 3) divisor = 4;							// Set minimum divisor limit
    LOG_DEBUG("Divisor = %d, Freq Set = %d\n", (int)divisor, (int)(41666667/divisor));
    return divisor;													// Return divisor that would be required
}

static SDRESULT sdSetClock (uint32_t freq)
{
    uint64_t td = 0;												// Zero time difference
    uint64_t start_time = 0;										// Zero start time
    LOG_DEBUG("EMMC: setting clock speed to %d.\n", (unsigned int)freq);
    while ((EMMC_STATUS->CMD_INHIBIT || EMMC_STATUS->DAT_INHIBIT)	// Data inhibit signal
             && (td < 100000))										// Timeout not reached
    {
        if (!start_time) start_time = timer_getTickCount64();					// If start time not set the set start time
            else td = tick_difference(start_time, timer_getTickCount64());			// Time difference between start time and now
    }
    if (td >= 100000) {												// Timeout waiting for inghibit flags
        printf("EMMC: Set clock: timeout waiting for inhibit flags. Status %08x.\n",
            (unsigned int)EMMC_STATUS->Raw32);
        return SD_ERROR_CLOCK;										// Return clock error
    }

    /* Switch clock off */
    EMMC_CONTROL1->CLK_EN = 0;										// Disable clock
    MicroDelay(10);													// We must now wait 10 microseconds

    /* Request the divisor for new clock setting */
    uint_fast32_t cdiv = sdGetClockDivider(freq);					// Fetch divisor for new frequency
    uint_fast32_t divlo = (cdiv & 0xff) << 8;						// Create divisor low bits value
    uint_fast32_t divhi = ((cdiv & 0x300) >> 2);					// Create divisor high bits value

    /* Set new clock frequency by setting new divisor */
    EMMC_CONTROL1->Raw32 = (EMMC_CONTROL1->Raw32 & 0xffff001f) | divlo | divhi;
    MicroDelay(10);													// We must now wait 10 microseconds

    /* Enable the clock. */
    EMMC_CONTROL1->CLK_EN = 1;										// Enable the clock

    /* Wait for clock to be stablize */
    td = 0;															// Zero time difference
    start_time = 0;													// Zero start time
    while (!(EMMC_CONTROL1->CLK_STABLE)								// Clock not stable yet
        && (td < 100000))											// Timeout not reached
    {
        if (!start_time) start_time = timer_getTickCount64();					// If start time not set the set start time
            else td = tick_difference(start_time, timer_getTickCount64());			// Time difference between start time and now
    }
    if (td >= 100000) {												// Timeout waiting for stability flag
        printf("EMMC: ERROR: failed to get stable clock.\n");
        return SD_ERROR_CLOCK;										// Return clock error
    }
    return SD_OK;													// Clock frequency set worked
}

static SDRESULT reset_sdio(void) {
    SDRESULT resp;
    uint64_t td = 0;												// Zero time difference
    uint64_t start_time = 0;	

    // Default guess the external clock here.
    // TODO: Get external clock
    emmc.extclk = FREQ_EXT;
    
    /* Send reset host controller and wait for complete */
    EMMC_CONTROL0->Raw32 = 0;   // Zero control0 register
    EMMC_CONTROL1->Raw32 = 0;   // Zero control1 register
    EMMC_CONTROL1->SRST_HC = 1; // Reset the complete host circuit
 
     MicroDelay(10);

    // Host circuit reset not clear and timeout is not reached
    while ((EMMC_CONTROL1->SRST_HC) && (td < 100000))
    {
        if (!start_time) {
            // If start time not set the set start time
            start_time = timer_getTickCount64();
        } else {
            // Time difference between start time and now
            td = tick_difference(start_time, timer_getTickCount64());
        }
    }
    if (td >= 100000) {
        // Timeout waiting for reset flag
        printf("EMMC: ERROR: failed to reset.\n");
        return SD_ERROR_RESET;
    }


    /* Enable internal clock and set data timeout */
    EMMC_CONTROL1->DATA_TOUNIT = 0xE;								// Maximum timeout value
    EMMC_CONTROL1->CLK_INTLEN = 1;									// Enable internal clock
    MicroDelay(10);		

    /* Set clock to setup frequency */
    if ( (resp = sdSetClock(FREQ_SETUP)) ) return resp;				// Set low speed setup frequency (400Khz)
    
    /* Enable interrupts for command completion values */
    EMMC_IRPT_EN->Raw32   = 0xffffffff;
    EMMC_IRPT_MASK->Raw32 = 0xffffffff;

	/* Reset our card structure entries */
	sdCard.rca = 0;													// Zero rca
	sdCard.lastCmd = 0;												// Zero lastCmd
	sdCard.status = 0;												// Zero status

    printf("Reset completed successfully. \n");
    return SD_OK;
}


SDRESULT initialize_sdio(void) {
    SDRESULT response;
    response = reset_sdio();
    if(response != SD_OK) {
        printf(" Error while reseting sdio %d \n", response);
    }
    // Use for sdio lines
    EMMC_CONTROL0->HCTL_DWIDTH = 1;
    return response;
}
