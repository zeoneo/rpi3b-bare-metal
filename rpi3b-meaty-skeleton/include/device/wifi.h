#ifndef _WIFI_H_
#define _WIFI_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <device/sdio.h>

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))	/* ceiling */
#define ROUNDDN(x, y)	(((x)/(y))*(y))		/* floor */
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define	PGROUND(s)	ROUNDUP(s, BY2PG)
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

	typedef struct Ctrl
	{
		uint32_t chip_id;
		uint32_t chip_rev;
		uint32_t chipcommon;
		uint32_t armctl;
		uint32_t armcore;
		uint32_t d11ctl;
		uint32_t socramctl;
		uint32_t socramregs;
		uint32_t socramsize;
		uint32_t rambase;
	};

	enum
	{
		SDIODEBUG = 0,
		SBDEBUG = 0,
		EVENTDEBUG = 0,
		VARDEBUG = 0,
		FWDEBUG = 0,

		Corescansz = 512,
		Uploadsz = 2048,

		Wifichan = 0, /* default channel */
		Firmwarecmp = 1,

		ARMcm3 = 0x82A,
		ARM7tdmi = 0x825,
		ARMcr4 = 0x83E,

		Fn0 = 0,
		Fn1 = 1,
		Fn2 = 2,
		Fbr1 = 0x100,
		Fbr2 = 0x200,

		/* CCCR */
		Ioenable = 0x02,
		Ioready = 0x03,
		Intenable = 0x04,
		Intpend = 0x05,
		Ioabort = 0x06,
		Busifc = 0x07,
		Capability = 0x08,
		Blksize = 0x10,
		Highspeed = 0x13,

		/* SDIOCommands */
		GO_IDLE_STATE = 0,
		SEND_RELATIVE_ADDR = 3,
		IO_SEND_OP_COND = 5,
		SELECT_CARD = 7,
		VOLTAGE_SWITCH = 11,
		IO_RW_DIRECT = 52,
		IO_RW_EXTENDED = 53,

		/* SELECT_CARD args */
		Rcashift = 16,

		/* SEND_OP_COND args */
		Hcs = 1 << 30,  /* host supports SDHC & SDXC */
		V3_3 = 3 << 20, /* 3.2-3.4 volts */
		V2_8 = 3 << 15, /* 2.7-2.9 volts */
		V2_0 = 1 << 8,  /* 2.0-2.1 volts */
		S18R = 1 << 24, /* switch to 1.8V request */

		/* core control regs */
		Ioctrl = 0x408,
		Resetctrl = 0x800,

		/* socram regs */
		Coreinfo = 0x00,
		Bankidx = 0x10,
		Bankinfo = 0x40,
		Bankpda = 0x44,

		/* armcr4 regs */
		Cr4Cap = 0x04,
		Cr4Bankidx = 0x40,
		Cr4Bankinfo = 0x44,
		Cr4Cpuhalt = 0x20,

		/* chipcommon regs */
		Gpiopullup = 0x58,
		Gpiopulldown = 0x5c,
		Chipctladdr = 0x650,
		Chipctldata = 0x654,

		/* sdio core regs */
		Intstatus = 0x20,
		Fcstate = 1 << 4,
		Fcchange = 1 << 5,
		FrameInt = 1 << 6,
		MailboxInt = 1 << 7,
		Intmask = 0x24,
		Sbmbox = 0x40,
		Sbmboxdata = 0x48,
		Hostmboxdata = 0x4c,
		Fwready = 0x80,

		/* wifi control commands */
		GetVar = 262,
		SetVar = 263,

		/* status */
		Disconnected = 0,
		Connecting,
		Connected,
	};

	void enable_wifi(void);
	uint32_t sdio_cmd(cmd_index_t cmd_index, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif