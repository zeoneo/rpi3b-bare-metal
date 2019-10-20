
/* bcm2835 external mass media controller (mmc / sd host interface)
 *
 * Copyright © 2012 Richard Miller <r.miller@acm.org>
 */

/*
	Not officially documented: emmc can be connected to different gpio pins
		48-53 (SD card)
		22-27 (P1 header)
		34-39 (wifi - pi3 only)
	using ALT3 function to activate the required routing
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

#define EMMCREGS (VIRTIO + 0x300000)

enum
{
	Extfreq = 100 * Mhz, /* guess external clock frequency if */
	/* not available from vcore */
	Initfreq = 400000,   /* initialisation frequency for MMC */
	SDfreq = 25 * Mhz,   /* standard SD frequency */
	SDfreqhs = 50 * Mhz, /* high speed frequency */
	DTO = 14,			 /* data timeout exponent (guesswork) */

	GoIdle = 0,			/* mmc/sdio go idle state */
	MMCSelect = 7,		/* mmc/sd card select command */
	Setbuswidth = 6,	/* mmc/sd set bus width command */
	Switchfunc = 6,		/* mmc/sd switch function command */
	Voltageswitch = 11, /* md/sdio switch to 1.8V */
	IORWdirect = 52,	/* sdio read/write direct command */
	IORWextended = 53,  /* sdio read/write extended command */
	Appcmd = 55,		/* mmc/sd application command prefix */
};

enum
{
	/* Controller registers */
	Arg2 = 0x00 >> 2,
	Blksizecnt = 0x04 >> 2,
	Arg1 = 0x08 >> 2,
	Cmdtm = 0x0c >> 2,
	Resp0 = 0x10 >> 2,
	Resp1 = 0x14 >> 2,
	Resp2 = 0x18 >> 2,
	Resp3 = 0x1c >> 2,
	Data = 0x20 >> 2,
	Status = 0x24 >> 2,
	Control0 = 0x28 >> 2,
	Control1 = 0x2c >> 2,
	Interrupt = 0x30 >> 2,
	Irptmask = 0x34 >> 2,
	Irpten = 0x38 >> 2,
	Control2 = 0x3c >> 2,
	Forceirpt = 0x50 >> 2,
	Boottimeout = 0x70 >> 2,
	Dbgsel = 0x74 >> 2,
	Exrdfifocfg = 0x80 >> 2,
	Exrdfifoen = 0x84 >> 2,
	Tunestep = 0x88 >> 2,
	Tunestepsstd = 0x8c >> 2,
	Tunestepsddr = 0x90 >> 2,
	Spiintspt = 0xf0 >> 2,
	Slotisrver = 0xfc >> 2,

	/* Control0 */
	Hispeed = 1 << 2,
	Dwidth4 = 1 << 1,
	Dwidth1 = 0 << 1,

	/* Control1 */
	Srstdata = 1 << 26, /* reset data circuit */
	Srstcmd = 1 << 25,  /* reset command circuit */
	Srsthc = 1 << 24,   /* reset complete host controller */
	Datatoshift = 16,   /* data timeout unit exponent */
	Datatomask = 0xF0000,
	Clkfreq8shift = 8, /* SD clock base divider LSBs */
	Clkfreq8mask = 0xFF00,
	Clkfreqms2shift = 6, /* SD clock base divider MSBs */
	Clkfreqms2mask = 0xC0,
	Clkgendiv = 0 << 5,  /* SD clock divided */
	Clkgenprog = 1 << 5, /* SD clock programmable */
	Clken = 1 << 2,		 /* SD clock enable */
	Clkstable = 1 << 1,
	Clkintlen = 1 << 0, /* enable internal EMMC clocks */

	/* Cmdtm */
	Indexshift = 24,
	Suspend = 1 << 22,
	Resume = 2 << 22,
	Abort = 3 << 22,
	Isdata = 1 << 21,
	Ixchken = 1 << 20,
	Crcchken = 1 << 19,
	Respmask = 3 << 16,
	Respnone = 0 << 16,
	Resp136 = 1 << 16,
	Resp48 = 2 << 16,
	Resp48busy = 3 << 16,
	Multiblock = 1 << 5,
	Host2card = 0 << 4,
	Card2host = 1 << 4,
	Autocmd12 = 1 << 2,
	Autocmd23 = 2 << 2,
	Blkcnten = 1 << 1,

	/* Interrupt */
	Acmderr = 1 << 24,
	Denderr = 1 << 22,
	Dcrcerr = 1 << 21,
	Dtoerr = 1 << 20,
	Cbaderr = 1 << 19,
	Cenderr = 1 << 18,
	Ccrcerr = 1 << 17,
	Ctoerr = 1 << 16,
	Err = 1 << 15,
	Cardintr = 1 << 8,
	Cardinsert = 1 << 6, /* not in Broadcom datasheet */
	Readrdy = 1 << 5,
	Writerdy = 1 << 4,
	Datadone = 1 << 1,
	Cmddone = 1 << 0,

	/* Status */
	Bufread = 1 << 11,  /* not in Broadcom datasheet */
	Bufwrite = 1 << 10, /* not in Broadcom datasheet */
	Readtrans = 1 << 9,
	Writetrans = 1 << 8,
	Datactive = 1 << 2,
	Datinhibit = 1 << 1,
	Cmdinhibit = 1 << 0,
};

static int cmdinfo[64] = {
	[0] Ixchken,
	[2] Resp136,
	[3] Resp48 | Ixchken | Crcchken,
	[5] Resp48,
	[6] Resp48 | Ixchken | Crcchken,
	[7] Resp48busy | Ixchken | Crcchken,
	[8] Resp48 | Ixchken | Crcchken,
	[9] Resp136,
	[11] Resp48 | Ixchken | Crcchken,
	[12] Resp48busy | Ixchken | Crcchken,
	[13] Resp48 | Ixchken | Crcchken,
	[16] Resp48,
	[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken,
	[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken,
	[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken,
	[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken,
	[41] Resp48,
	[52] Resp48 | Ixchken | Crcchken,
	[53] Resp48 | Ixchken | Crcchken | Isdata,
	[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Ctlr Ctlr;

struct Ctlr
{
	Rendez r;
	Rendez cardr;
	int fastclock;
	ulong extclk;
	int appcmd;
};

static Ctlr emmc;

static void mmcinterrupt(Ureg *, void *);

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int *)EMMCREGS;

	if (0)
		print("WR %2.2ux %ux\n", reg << 2, val);
	microdelay(emmc.fastclock ? 2 : 20);
	coherence();
	r[reg] = val;
}

static uint
clkdiv(uint d)
{
	uint v;

	assert(d < 1 << 10);
	v = (d << Clkfreq8shift) & Clkfreq8mask;
	v |= ((d >> 8) << Clkfreqms2shift) & Clkfreqms2mask;
	return v;
}

static void
emmcclk(uint freq)
{
	u32int *r;
	uint div;
	int i;

	r = (u32int *)EMMCREGS;
	div = emmc.extclk / (freq << 1);
	if (emmc.extclk / (div << 1) > freq)
		div++;
	WR(Control1, clkdiv(div) |
					 DTO << Datatoshift | Clkgendiv | Clken | Clkintlen);
	for (i = 0; i < 1000; i++)
	{
		delay(1);
		if (r[Control1] & Clkstable)
			break;
	}
	if (i == 1000)
		print("emmc: can't set clock to %ud\n", freq);
}

static int
datadone(void *)
{
	int i;

	u32int *r = (u32int *)EMMCREGS;
	i = r[Interrupt];
	return i & (Datadone | Err);
}

static int
cardintready(void *)
{
	int i;

	u32int *r = (u32int *)EMMCREGS;
	i = r[Interrupt];
	return i & Cardintr;
}

static int
emmcinit(void)
{
	u32int *r;
	ulong clk;

	clk = getclkrate(ClkEmmc);
	if (clk == 0)
	{
		clk = Extfreq;
		print("emmc: assuming external clock %lud Mhz\n", clk / 1000000);
	}
	emmc.extclk = clk;
	r = (u32int *)EMMCREGS;
	if (0)
		print("emmc control %8.8ux %8.8ux %8.8ux\n",
			  r[Control0], r[Control1], r[Control2]);
	WR(Control1, Srsthc);
	delay(10);
	while (r[Control1] & Srsthc)
		;
	WR(Control1, Srstdata);
	delay(10);
	WR(Control1, 0);
	return 0;
}

static int
emmcinquiry(char *inquiry, int inqlen)
{
	u32int *r;
	uint ver;

	r = (u32int *)EMMCREGS;
	ver = r[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
				   "Arasan eMMC SD Host Controller %2.2x Version %2.2x",
				   ver & 0xFF, ver >> 8);
}

static void
emmcenable(void)
{
	emmcclk(Initfreq);
	WR(Irpten, 0);
	WR(Irptmask, ~0);
	WR(Interrupt, ~0);
	intrenable(IRQmmc, mmcinterrupt, nil, 0, "mmc");
}

int sdiocardintr(int wait)
{
	u32int *r;
	int i;

	r = (u32int *)EMMCREGS;
	WR(Interrupt, Cardintr);
	while (((i = r[Interrupt]) & Cardintr) == 0)
	{
		if (!wait)
			return 0;
		WR(Irpten, r[Irpten] | Cardintr);
		sleep(&emmc.cardr, cardintready, 0);
	}
	WR(Interrupt, Cardintr);
	return i;
}

static int
emmccmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i;
	ulong now;

	r = (u32int *)EMMCREGS;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if (cmd == Switchfunc && !emmc.appcmd)
		c |= Isdata | Card2host;
	if (cmd == IORWextended)
	{
		if (arg & (1 << 31))
			c |= Host2card;
		else
			c |= Card2host;
		if ((r[Blksizecnt] & 0xFFFF0000) != 0x10000)
			c |= Multiblock | Blkcnten;
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if (cmd == GoIdle)
	{
		WR(Control0, r[Control0] & ~(Dwidth4 | Hispeed));
		emmcclk(Initfreq);
	}
	if (r[Status] & Cmdinhibit)
	{
		print("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n",
			  r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while (r[Control1] & Srstcmd)
			;
		while (r[Status] & Cmdinhibit)
			;
	}
	if ((r[Status] & Datinhibit) &&
		((c & Isdata) || (c & Respmask) == Resp48busy))
	{
		print("emmccmd: need to reset Datinhibit intr %ux stat %ux\n",
			  r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while (r[Control1] & Srstdata)
			;
		while (r[Status] & Datinhibit)
			;
	}
	WR(Arg1, arg);
	if ((i = (r[Interrupt] & ~Cardintr)) != 0)
	{
		if (i != Cardinsert)
			print("emmc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = m->ticks;
	while (((i = r[Interrupt]) & (Cmddone | Err)) == 0)
		if (m->ticks - now > HZ)
			break;
	if ((i & (Cmddone | Err)) != Cmddone)
	{
		if ((i & ~(Err | Cardintr)) != Ctoerr)
			print("emmc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, r[Status]);
		WR(Interrupt, i);
		if (r[Status] & Cmdinhibit)
		{
			WR(Control1, r[Control1] | Srstcmd);
			while (r[Control1] & Srstcmd)
				;
		}
		error(Eio);
	}
	WR(Interrupt, i & ~(Datadone | Readrdy | Writerdy));
	switch (c & Respmask)
	{
	case Resp136:
		resp[0] = r[Resp0] << 8;
		resp[1] = r[Resp0] >> 24 | r[Resp1] << 8;
		resp[2] = r[Resp1] >> 24 | r[Resp2] << 8;
		resp[3] = r[Resp2] >> 24 | r[Resp3] << 8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if ((c & Respmask) == Resp48busy)
	{
		WR(Irpten, r[Irpten] | Datadone | Err);
		tsleep(&emmc.r, datadone, 0, 3000);
		i = r[Interrupt];
		if ((i & Datadone) == 0)
			print("emmcio: no Datadone after CMD%d\n", cmd);
		if (i & Err)
			print("emmcio: CMD%d error interrupt %ux\n",
				  cmd, r[Interrupt]);
		WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if (cmd == MMCSelect)
	{
		delay(1);
		emmcclk(SDfreq);
		delay(1);
		emmc.fastclock = 1;
	}
	if (cmd == Setbuswidth)
	{
		if (emmc.appcmd)
		{
			/*
			 * If card bus width changes, change host bus width
			 */
			switch (arg)
			{
			case 0:
				WR(Control0, r[Control0] & ~Dwidth4);
				break;
			case 2:
				WR(Control0, r[Control0] | Dwidth4);
				break;
			}
		}
		else
		{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if ((arg & 0x8000000F) == 0x80000001)
			{
				delay(1);
				emmcclk(SDfreqhs);
				delay(1);
			}
		}
	}
	else if (cmd == IORWdirect && (arg & ~0xFF) == (1 << 31 | 0 << 28 | 7 << 9))
	{
		switch (arg & 0x3)
		{
		case 0:
			WR(Control0, r[Control0] & ~Dwidth4);
			break;
		case 2:
			WR(Control0, r[Control0] | Dwidth4);
			//WR(Control0, r[Control0] | Hispeed);
			break;
		}
	}
	emmc.appcmd = (cmd == Appcmd);
	return 0;
}

void emmciosetup(int write, void *buf, int bsize, int bcount)
{
	USED(write);
	USED(buf);
	WR(Blksizecnt, bcount << 16 | bsize);
}

static void
emmcio(int write, uchar *buf, int len)
{
	u32int *r;
	int i;

	r = (u32int *)EMMCREGS;
	assert((len & 3) == 0);
	okay(1);
	if (waserror())
	{
		okay(0);
		nexterror();
	}
	if (write)
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaM2D,
				 buf, &r[Data], len);
	else
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaD2M,
				 &r[Data], buf, len);
	if (dmawait(DmaChanEmmc) < 0)
		error(Eio);
	if (!write)
		cachedinvse(buf, len);
	WR(Irpten, r[Irpten] | Datadone | Err);
	tsleep(&emmc.r, datadone, 0, 3000);
	i = r[Interrupt] & ~Cardintr;
	if ((i & Datadone) == 0)
	{
		print("emmcio: %d timeout intr %ux stat %ux\n",
			  write, i, r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if (i & Err)
	{
		print("emmcio: %d error intr %ux stat %ux\n",
			  write, r[Interrupt], r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if (i)
		WR(Interrupt, i);
	poperror();
	okay(0);
}

static void
mmcinterrupt(Ureg *, void *)
{
	u32int *r;
	int i;

	r = (u32int *)EMMCREGS;
	i = r[Interrupt];
	if (i & (Datadone | Err))
		wakeup(&emmc.r);
	if (i & Cardintr)
		wakeup(&emmc.cardr);
	WR(Irpten, r[Irpten] & ~i);
}

SDio sdio = {
	"emmc",
	emmcinit,
	emmcenable,
	emmcinquiry,
	emmccmd,
	emmciosetup,
	emmcio,
};
etbuswidth = 6,		/* mmc/sd set bus width command */
	Switchfunc = 6, /* mmc/sd switch fuether4330.c                                                                                            664       0       0       136726 13526512635  11066                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Broadcom bcm4330 wifi (sdio interface)
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/sd.h"

	extern int sdiocardintr(int);

#include "etherif.h"
#define CACHELINESZ 64 /* temp */

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

	/* Sonics Silicon Backplane (access to cores on chip) */
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

typedef struct Ctlr Ctlr;

enum
{
	Wpa = 1,
	Wep = 2,
	Wpa2 = 3,
	WNameLen = 32,
	WNKeys = 4,
	WKeyLen = 32,
	WMinKeyLen = 5,
	WMaxKeyLen = 13,
};

typedef struct WKey WKey;
struct WKey
{
	ushort len;
	char dat[WKeyLen];
};

struct Ctlr
{
	Ether *edev;
	QLock cmdlock;
	QLock pktlock;
	QLock tlock;
	QLock alock;
	Lock txwinlock;
	Rendez cmdr;
	Rendez joinr;
	int joinstatus;
	int cryptotype;
	int chanid;
	char essid[WNameLen + 1];
	WKey keys[WNKeys];
	Block *rsp;
	Block *scanb;
	int scansecs;
	int status;
	int chipid;
	int chiprev;
	int armcore;
	char *regufile;
	union {
		u32int i;
		uchar c[4];
	} resetvec;
	ulong chipcommon;
	ulong armctl;
	ulong armregs;
	ulong d11ctl;
	ulong socramregs;
	ulong socramctl;
	ulong sdregs;
	int sdiorev;
	int socramrev;
	ulong socramsize;
	ulong rambase;
	short reqid;
	uchar fcmask;
	uchar txwindow;
	uchar txseq;
	uchar rxseq;
};

enum
{
	CMauth,
	CMchannel,
	CMcrypt,
	CMessid,
	CMkey1,
	CMkey2,
	CMkey3,
	CMkey4,
	CMrxkey,
	CMrxkey0,
	CMrxkey1,
	CMrxkey2,
	CMrxkey3,
	CMtxkey,
	CMdebug,
	CMjoin,
};

static Cmdtab cmds[] = {
	{CMauth, "auth", 2},
	{CMchannel, "channel", 2},
	{CMcrypt, "crypt", 2},
	{CMessid, "essid", 2},
	{CMkey1, "key1", 2},
	{CMkey2, "key1", 2},
	{CMkey3, "key1", 2},
	{CMkey4, "key1", 2},
	{CMrxkey, "rxkey", 3},
	{CMrxkey0, "rxkey0", 3},
	{CMrxkey1, "rxkey1", 3},
	{CMrxkey2, "rxkey2", 3},
	{CMrxkey3, "rxkey3", 3},
	{CMtxkey, "txkey", 3},
	{CMdebug, "debug", 2},
	{CMjoin, "join", 5},
};

typedef struct Sdpcm Sdpcm;
typedef struct Cmd Cmd;
struct Sdpcm
{
	uchar len[2];
	uchar lenck[2];
	uchar seq;
	uchar chanflg;
	uchar nextlen;
	uchar doffset;
	uchar fcmask;
	uchar window;
	uchar version;
	uchar pad;
};

struct Cmd
{
	uchar cmd[4];
	uchar len[4];
	uchar flags[2];
	uchar id[2];
	uchar status[4];
};

static char config40181[] = "bcmdhd.cal.40181";
static char config40183[] = "bcmdhd.cal.40183.26MHz";

struct
{
	int chipid;
	int chiprev;
	char *fwfile;
	char *cfgfile;
	char *regufile;
} firmware[] = {
	{0x4330, 3, "fw_bcm40183b1.bin", config40183, 0},
	{0x4330, 4, "fw_bcm40183b2.bin", config40183, 0},
	{43362, 0, "fw_bcm40181a0.bin", config40181, 0},
	{43362, 1, "fw_bcm40181a2.bin", config40181, 0},
	{43430, 1, "brcmfmac43430-sdio.bin", "brcmfmac43430-sdio.txt", 0},
	{0x4345, 6, "brcmfmac43455-sdio.bin", "brcmfmac43455-sdio.txt", "brcmfmac43455-sdio.clm_blob"},
};

static QLock sdiolock;
static int iodebug;

static void etherbcmintr(void *);
static void bcmevent(Ctlr *, uchar *, int);
static void wlscanresult(Ether *, uchar *, int);
static void wlsetvar(Ctlr *, char *, void *, int);

static uchar *
put2(uchar *p, short v)
{
	p[0] = v;
	p[1] = v >> 8;
	return p + 2;
}

static uchar *
put4(uchar *p, long v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
	return p + 4;
}

static ushort
get2(uchar *p)
{
	return p[0] | p[1] << 8;
}

static ulong
get4(uchar *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static void
dump(char *s, void *a, int n)
{
	int i;
	uchar *p;

	p = a;
	print("%s:", s);
	for (i = 0; i < n; i++)
		print("%c%2.2x", i & 15 ? ' ' : '\n', *p++);
	print("\n");
}

/*
 * SDIO communication with dongle
 */
static ulong
sdiocmd_locked(int cmd, ulong arg)
{
	u32int resp[4];

	sdio.cmd(cmd, arg, resp);
	return resp[0];
}

static ulong
sdiocmd(int cmd, ulong arg)
{
	ulong r;

	qlock(&sdiolock);
	if (waserror())
	{
		if (SDIODEBUG)
			print("sdiocmd error: cmd %d arg %lux\n", cmd, arg);
		qunlock(&sdiolock);
		nexterror();
	}
	r = sdiocmd_locked(cmd, arg);
	qunlock(&sdiolock);
	poperror();
	return r;
}

static ulong
trysdiocmd(int cmd, ulong arg)
{
	ulong r;

	if (waserror())
		return 0;
	r = sdiocmd(cmd, arg);
	poperror();
	return r;
}

static int
sdiord(int fn, int addr)
{
	int r;

	r = sdiocmd(IO_RW_DIRECT, (0 << 31) | ((fn & 7) << 28) | ((addr & 0x1FFFF) << 9));
	if (r & 0xCF00)
	{
		print("ether4330: sdiord(%x, %x) fail: %2.2ux %2.2ux\n", fn, addr, (r >> 8) & 0xFF, r & 0xFF);
		error(Eio);
	}
	return r & 0xFF;
}

static void
sdiowr(int fn, int addr, int data)
{
	int r;
	int retry;

	r = 0;
	for (retry = 0; retry < 10; retry++)
	{
		r = sdiocmd(IO_RW_DIRECT, (1 << 31) | ((fn & 7) << 28) | ((addr & 0x1FFFF) << 9) | (data & 0xFF));
		if ((r & 0xCF00) == 0)
			return;
	}
	print("ether4330: sdiowr(%x, %x, %x) fail: %2.2ux %2.2ux\n", fn, addr, data, (r >> 8) & 0xFF, r & 0xFF);
	error(Eio);
}

static void
sdiorwext(int fn, int write, void *a, int len, int addr, int incr)
{
	int bsize, blk, bcount, m;

	bsize = fn == Fn2 ? 512 : 64;
	while (len > 0)
	{
		if (len >= 511 * bsize)
		{
			blk = 1;
			bcount = 511;
			m = bcount * bsize;
		}
		else if (len > bsize)
		{
			blk = 1;
			bcount = len / bsize;
			m = bcount * bsize;
		}
		else
		{
			blk = 0;
			bcount = len;
			m = bcount;
		}
		qlock(&sdiolock);
		if (waserror())
		{
			print("ether4330: sdiorwext fail: %s\n", up->errstr);
			qunlock(&sdiolock);
			nexterror();
		}
		if (blk)
			sdio.iosetup(write, a, bsize, bcount);
		else
			sdio.iosetup(write, a, bcount, 1);
		sdiocmd_locked(IO_RW_EXTENDED,
					   write << 31 | (fn & 7) << 28 | blk << 27 | incr << 26 | (addr & 0x1FFFF) << 9 | (bcount & 0x1FF));
		sdio.io(write, a, m);
		qunlock(&sdiolock);
		poperror();
		len -= m;
		a = (char *)a + m;
		if (incr)
			addr += m;
	}
}

static void
sdioset(int fn, int addr, int bits)
{
	sdiowr(fn, addr, sdiord(fn, addr) | bits);
}

static void
sdioinit(void)
{
	ulong ocr, rca;
	int i;

	/* disconnect emmc from SD card (connect sdhost instead) */
	for (i = 48; i <= 53; i++)
		gpiosel(i, Alt0);
	/* connect emmc to wifi */
	for (i = 34; i <= 39; i++)
	{
		gpiosel(i, Alt3);
		if (i == 34)
			gpiopulloff(i);
		else
			gpiopullup(i);
	}
	sdio.init();
	sdio.enable();
	sdiocmd(GO_IDLE_STATE, 0);
	ocr = trysdiocmd(IO_SEND_OP_COND, 0);
	i = 0;
	while ((ocr & (1 << 31)) == 0)
	{
		if (++i > 5)
		{
			print("ether4330: no response to sdio access: ocr = %lux\n", ocr);
			error(Eio);
		}
		ocr = trysdiocmd(IO_SEND_OP_COND, V3_3);
		tsleep(&up->sleep, return0, nil, 100);
	}
	rca = sdiocmd(SEND_RELATIVE_ADDR, 0) >> Rcashift;
	sdiocmd(SELECT_CARD, rca << Rcashift);
	sdioset(Fn0, Highspeed, 2);
	sdioset(Fn0, Busifc, 2); /* bus width 4 */
	sdiowr(Fn0, Fbr1 + Blksize, 64);
	sdiowr(Fn0, Fbr1 + Blksize + 1, 64 >> 8);
	sdiowr(Fn0, Fbr2 + Blksize, 512);
	sdiowr(Fn0, Fbr2 + Blksize + 1, 512 >> 8);
	sdioset(Fn0, Ioenable, 1 << Fn1);
	sdiowr(Fn0, Intenable, 0);
	for (i = 0; !(sdiord(Fn0, Ioready) & 1 << Fn1); i++)
	{
		if (i == 10)
		{
			print("ether4330: can't enable SDIO function\n");
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
}

static void
sdioreset(void)
{
	sdiowr(Fn0, Ioabort, 1 << 3); /* reset */
}

static void
sdioabort(int fn)
{
	sdiowr(Fn0, Ioabort, fn);
}

/*
 * Chip register and memory access via SDIO
 */

static void
cfgw(ulong off, int val)
{
	sdiowr(Fn1, off, val);
}

static int
cfgr(ulong off)
{
	return sdiord(Fn1, off);
}

static ulong
cfgreadl(int fn, ulong off)
{
	uchar cbuf[2 * CACHELINESZ];
	uchar *p;

	p = (uchar *)ROUND((uintptr)cbuf, CACHELINESZ);
	memset(p, 0, 4);
	sdiorwext(fn, 0, p, 4, off | Sb32bit, 1);
	if (SDIODEBUG)
		print("cfgreadl %lux: %2.2x %2.2x %2.2x %2.2x\n", off, p[0], p[1], p[2], p[3]);
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static void
cfgwritel(int fn, ulong off, u32int data)
{
	uchar cbuf[2 * CACHELINESZ];
	uchar *p;
	int retry;

	p = (uchar *)ROUND((uintptr)cbuf, CACHELINESZ);
	put4(p, data);
	if (SDIODEBUG)
		print("cfgwritel %lux: %2.2x %2.2x %2.2x %2.2x\n", off, p[0], p[1], p[2], p[3]);
	retry = 0;
	while (waserror())
	{
		print("ether4330: cfgwritel retry %lux %ux\n", off, data);
		sdioabort(fn);
		if (++retry == 3)
			nexterror();
	}
	sdiorwext(fn, 1, p, 4, off | Sb32bit, 1);
	poperror();
}

static void
sbwindow(ulong addr)
{
	addr &= ~(Sbwsize - 1);
	cfgw(Sbaddr, addr >> 8);
	cfgw(Sbaddr + 1, addr >> 16);
	cfgw(Sbaddr + 2, addr >> 24);
}

static void
sbrw(int fn, int write, uchar *buf, int len, ulong off)
{
	int n;
	USED(fn);

	if (waserror())
	{
		print("ether4330: sbrw err off %lux len %ud\n", off, len);
		nexterror();
	}
	if (write)
	{
		if (len >= 4)
		{
			n = len;
			n &= ~3;
			sdiorwext(Fn1, write, buf, n, off | Sb32bit, 1);
			off += n;
			buf += n;
			len -= n;
		}
		while (len > 0)
		{
			sdiowr(Fn1, off | Sb32bit, *buf);
			off++;
			buf++;
			len--;
		}
	}
	else
	{
		if (len >= 4)
		{
			n = len;
			n &= ~3;
			sdiorwext(Fn1, write, buf, n, off | Sb32bit, 1);
			off += n;
			buf += n;
			len -= n;
		}
		while (len > 0)
		{
			*buf = sdiord(Fn1, off | Sb32bit);
			off++;
			buf++;
			len--;
		}
	}
	poperror();
}

static void
sbmem(int write, uchar *buf, int len, ulong off)
{
	ulong n;

	n = ROUNDUP(off, Sbwsize) - off;
	if (n == 0)
		n = Sbwsize;
	while (len > 0)
	{
		if (n > len)
			n = len;
		sbwindow(off);
		sbrw(Fn1, write, buf, n, off & (Sbwsize - 1));
		off += n;
		buf += n;
		len -= n;
		n = Sbwsize;
	}
}

static void
packetrw(int write, uchar *buf, int len)
{
	int n;
	int retry;

	n = 2048;
	while (len > 0)
	{
		if (n > len)
			n = ROUND(len, 4);
		retry = 0;
		while (waserror())
		{
			sdioabort(Fn2);
			if (++retry == 3)
				nexterror();
		}
		sdiorwext(Fn2, write, buf, n, Enumbase, 0);
		poperror();
		buf += n;
		len -= n;
	}
}

/*
 * Configuration and control of chip cores via Silicon Backplane
 */

static void
sbdisable(ulong regs, int pre, int ioctl)
{
	sbwindow(regs);
	if ((cfgreadl(Fn1, regs + Resetctrl) & 1) != 0)
	{
		cfgwritel(Fn1, regs + Ioctrl, 3 | ioctl);
		cfgreadl(Fn1, regs + Ioctrl);
		return;
	}
	cfgwritel(Fn1, regs + Ioctrl, 3 | pre);
	cfgreadl(Fn1, regs + Ioctrl);
	cfgwritel(Fn1, regs + Resetctrl, 1);
	microdelay(10);
	while ((cfgreadl(Fn1, regs + Resetctrl) & 1) == 0)
		;
	cfgwritel(Fn1, regs + Ioctrl, 3 | ioctl);
	cfgreadl(Fn1, regs + Ioctrl);
}

static void
sbreset(ulong regs, int pre, int ioctl)
{
	sbdisable(regs, pre, ioctl);
	sbwindow(regs);
	if (SBDEBUG)
		print("sbreset %#p %#lux %#lux ->", regs,
			  cfgreadl(Fn1, regs + Ioctrl), cfgreadl(Fn1, regs + Resetctrl));
	while ((cfgreadl(Fn1, regs + Resetctrl) & 1) != 0)
	{
		cfgwritel(Fn1, regs + Resetctrl, 0);
		microdelay(40);
	}
	cfgwritel(Fn1, regs + Ioctrl, 1 | ioctl);
	cfgreadl(Fn1, regs + Ioctrl);
	if (SBDEBUG)
		print("%#lux %#lux\n",
			  cfgreadl(Fn1, regs + Ioctrl), cfgreadl(Fn1, regs + Resetctrl));
}

static void
corescan(Ctlr *ctl, ulong r)
{
	uchar *buf;
	int i, coreid, corerev;
	ulong addr;

	buf = sdmalloc(Corescansz);
	if (buf == nil)
		error(Enomem);
	sbmem(0, buf, Corescansz, r);
	coreid = 0;
	corerev = 0;
	for (i = 0; i < Corescansz; i += 4)
	{
		switch (buf[i] & 0xF)
		{
		case 0xF: /* end */
			sdfree(buf);
			return;
		case 0x1: /* core info */
			if ((buf[i + 4] & 0xF) != 0x1)
				break;
			coreid = (buf[i + 1] | buf[i + 2] << 8) & 0xFFF;
			i += 4;
			corerev = buf[i + 3];
			break;
		case 0x05: /* address */
			addr = buf[i + 1] << 8 | buf[i + 2] << 16 | buf[i + 3] << 24;
			addr &= ~0xFFF;
			if (SBDEBUG)
				print("core %x %s %#p\n", coreid, buf[i] & 0xC0 ? "ctl" : "mem", addr);
			switch (coreid)
			{
			case 0x800:
				if ((buf[i] & 0xC0) == 0)
					ctl->chipcommon = addr;
				break;
			case ARMcm3:
			case ARM7tdmi:
			case ARMcr4:
				ctl->armcore = coreid;
				if (buf[i] & 0xC0)
				{
					if (ctl->armctl == 0)
						ctl->armctl = addr;
				}
				else
				{
					if (ctl->armregs == 0)
						ctl->armregs = addr;
				}
				break;
			case 0x80E:
				if (buf[i] & 0xC0)
					ctl->socramctl = addr;
				else if (ctl->socramregs == 0)
					ctl->socramregs = addr;
				ctl->socramrev = corerev;
				break;
			case 0x829:
				if ((buf[i] & 0xC0) == 0)
					ctl->sdregs = addr;
				ctl->sdiorev = corerev;
				break;
			case 0x812:
				if (buf[i] & 0xC0)
					ctl->d11ctl = addr;
				break;
			}
		}
	}
	sdfree(buf);
}

static void
ramscan(Ctlr *ctl)
{
	ulong r, n, size;
	int banks, i;

	if (ctl->armcore == ARMcr4)
	{
		r = ctl->armregs;
		sbwindow(r);
		n = cfgreadl(Fn1, r + Cr4Cap);
		if (SBDEBUG)
			print("cr4 banks %lux\n", n);
		banks = ((n >> 4) & 0xF) + (n & 0xF);
		size = 0;
		for (i = 0; i < banks; i++)
		{
			cfgwritel(Fn1, r + Cr4Bankidx, i);
			n = cfgreadl(Fn1, r + Cr4Bankinfo);
			if (SBDEBUG)
				print("bank %d reg %lux size %lud\n", i, n, 8192 * ((n & 0x3F) + 1));
			size += 8192 * ((n & 0x3F) + 1);
		}
		ctl->socramsize = size;
		ctl->rambase = 0x198000;
		return;
	}
	if (ctl->socramrev <= 7 || ctl->socramrev == 12)
	{
		print("ether4330: SOCRAM rev %d not supported\n", ctl->socramrev);
		error(Eio);
	}
	sbreset(ctl->socramctl, 0, 0);
	r = ctl->socramregs;
	sbwindow(r);
	n = cfgreadl(Fn1, r + Coreinfo);
	if (SBDEBUG)
		print("socramrev %d coreinfo %lux\n", ctl->socramrev, n);
	banks = (n >> 4) & 0xF;
	size = 0;
	for (i = 0; i < banks; i++)
	{
		cfgwritel(Fn1, r + Bankidx, i);
		n = cfgreadl(Fn1, r + Bankinfo);
		if (SBDEBUG)
			print("bank %d reg %lux size %lud\n", i, n, 8192 * ((n & 0x3F) + 1));
		size += 8192 * ((n & 0x3F) + 1);
	}
	ctl->socramsize = size;
	ctl->rambase = 0;
	if (ctl->chipid == 43430)
	{
		cfgwritel(Fn1, r + Bankidx, 3);
		cfgwritel(Fn1, r + Bankpda, 0);
	}
}

static void
sbinit(Ctlr *ctl)
{
	ulong r;
	int chipid;
	char buf[16];

	sbwindow(Enumbase);
	r = cfgreadl(Fn1, Enumbase);
	chipid = r & 0xFFFF;
	sprint(buf, chipid > 43000 ? "%d" : "%#x", chipid);
	print("ether4330: chip %s rev %ld type %ld\n", buf, (r >> 16) & 0xF, (r >> 28) & 0xF);
	switch (chipid)
	{
	case 0x4330:
	case 43362:
	case 43430:
	case 0x4345:
		ctl->chipid = chipid;
		ctl->chiprev = (r >> 16) & 0xF;
		break;
	default:
		print("ether4330: chipid %#x (%d) not supported\n", chipid, chipid);
		error(Eio);
	}
	r = cfgreadl(Fn1, Enumbase + 63 * 4);
	corescan(ctl, r);
	if (ctl->armctl == 0 || ctl->d11ctl == 0 ||
		(ctl->armcore == ARMcm3 && (ctl->socramctl == 0 || ctl->socramregs == 0)))
		error("corescan didn't find essential cores\n");
	if (ctl->armcore == ARMcr4)
		sbreset(ctl->armctl, Cr4Cpuhalt, Cr4Cpuhalt);
	else
		sbdisable(ctl->armctl, 0, 0);
	sbreset(ctl->d11ctl, 8 | 4, 4);
	ramscan(ctl);
	if (SBDEBUG)
		print("ARM %#p D11 %#p SOCRAM %#p,%#p %lud bytes @ %#p\n",
			  ctl->armctl, ctl->d11ctl, ctl->socramctl, ctl->socramregs, ctl->socramsize, ctl->rambase);
	cfgw(Clkcsr, 0);
	microdelay(10);
	if (SBDEBUG)
		print("chipclk: %x\n", cfgr(Clkcsr));
	cfgw(Clkcsr, Nohwreq | ReqALP);
	while ((cfgr(Clkcsr) & (HTavail | ALPavail)) == 0)
		microdelay(10);
	cfgw(Clkcsr, Nohwreq | ForceALP);
	microdelay(65);
	if (SBDEBUG)
		print("chipclk: %x\n", cfgr(Clkcsr));
	cfgw(Pullups, 0);
	sbwindow(ctl->chipcommon);
	cfgwritel(Fn1, ctl->chipcommon + Gpiopullup, 0);
	cfgwritel(Fn1, ctl->chipcommon + Gpiopulldown, 0);
	if (ctl->chipid != 0x4330 && ctl->chipid != 43362)
		return;
	cfgwritel(Fn1, ctl->chipcommon + Chipctladdr, 1);
	if (cfgreadl(Fn1, ctl->chipcommon + Chipctladdr) != 1)
		print("ether4330: can't set Chipctladdr\n");
	else
	{
		r = cfgreadl(Fn1, ctl->chipcommon + Chipctldata);
		if (SBDEBUG)
			print("chipcommon PMU (%lux) %lux", cfgreadl(Fn1, ctl->chipcommon + Chipctladdr), r);
		/* set SDIO drive strength >= 6mA */
		r &= ~0x3800;
		if (ctl->chipid == 0x4330)
			r |= 3 << 11;
		else
			r |= 7 << 11;
		cfgwritel(Fn1, ctl->chipcommon + Chipctldata, r);
		if (SBDEBUG)
			print("-> %lux (= %lux)\n", r, cfgreadl(Fn1, ctl->chipcommon + Chipctldata));
	}
}

static void
sbenable(Ctlr *ctl)
{
	int i;

	if (SBDEBUG)
		print("enabling HT clock...");
	cfgw(Clkcsr, 0);
	delay(1);
	cfgw(Clkcsr, ReqHT);
	for (i = 0; (cfgr(Clkcsr) & HTavail) == 0; i++)
	{
		if (i == 50)
		{
			print("ether4330: can't enable HT clock: csr %x\n", cfgr(Clkcsr));
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
	cfgw(Clkcsr, cfgr(Clkcsr) | ForceHT);
	delay(10);
	if (SBDEBUG)
		print("chipclk: %x\n", cfgr(Clkcsr));
	sbwindow(ctl->sdregs);
	cfgwritel(Fn1, ctl->sdregs + Sbmboxdata, 4 << 16); /* protocol version */
	cfgwritel(Fn1, ctl->sdregs + Intmask, FrameInt | MailboxInt | Fcchange);
	sdioset(Fn0, Ioenable, 1 << Fn2);
	for (i = 0; !(sdiord(Fn0, Ioready) & 1 << Fn2); i++)
	{
		if (i == 10)
		{
			print("ether4330: can't enable SDIO function 2 - ioready %x\n", sdiord(Fn0, Ioready));
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
	sdiowr(Fn0, Intenable, (1 << Fn1) | (1 << Fn2) | 1);
}

/*
 * Firmware and config file uploading
 */

/*
 * Condense config file contents (in buffer buf with length n)
 * to 'var=value\0' list for firmware:
 *	- remove comments (starting with '#') and blank lines
 *	- remove carriage returns
 *	- convert newlines to nulls
 *	- mark end with two nulls
 *	- pad with nulls to multiple of 4 bytes total length
 */
static int
condense(uchar *buf, int n)
{
	uchar *p, *ep, *lp, *op;
	int c, skipping;

	skipping = 0; /* true if in a comment */
	ep = buf + n; /* end of input */
	op = buf;	 /* end of output */
	lp = buf;	 /* start of current output line */
	for (p = buf; p < ep; p++)
	{
		switch (c = *p)
		{
		case '#':
			skipping = 1;
			break;
		case '\0':
		case '\n':
			skipping = 0;
			if (op != lp)
			{
				*op++ = '\0';
				lp = op;
			}
			break;
		case '\r':
			break;
		default:
			if (!skipping)
				*op++ = c;
			break;
		}
	}
	if (!skipping && op != lp)
		*op++ = '\0';
	*op++ = '\0';
	for (n = op - buf; n & 03; n++)
		*op++ = '\0';
	return n;
}

/*
 * Try to find firmware file in /boot or in /sys/lib/firmware.
 * Throw an error if not found.
 */
static Chan *
findfirmware(char *file)
{
	char nbuf[64];
	Chan *c;

	if (!waserror())
	{
		snprint(nbuf, sizeof nbuf, "/boot/%s", file);
		c = namec(nbuf, Aopen, OREAD, 0);
		poperror();
	}
	else if (!waserror())
	{
		snprint(nbuf, sizeof nbuf, "/sys/lib/firmware/%s", file);
		c = namec(nbuf, Aopen, OREAD, 0);
		poperror();
	}
	else
	{
		c = nil;
		snprint(up->genbuf, sizeof up->genbuf, "can't find %s in /boot or /sys/lib/firmware", file);
		error(up->genbuf);
	}
	return c;
}

static int
upload(Ctlr *ctl, char *file, int isconfig)
{
	Chan *c;
	uchar *buf;
	uchar *cbuf;
	int off, n;

	buf = cbuf = nil;
	c = findfirmware(file);
	if (waserror())
	{
		cclose(c);
		sdfree(buf);
		sdfree(cbuf);
		nexterror();
	}
	buf = sdmalloc(Uploadsz);
	if (buf == nil)
		error(Enomem);
	if (Firmwarecmp)
	{
		cbuf = sdmalloc(Uploadsz);
		if (cbuf == nil)
			error(Enomem);
	}
	off = 0;
	for (;;)
	{
		n = devtab[c->type]->read(c, buf, Uploadsz, off);
		if (n <= 0)
			break;
		if (isconfig)
		{
			n = condense(buf, n);
			off = ctl->socramsize - n - 4;
		}
		else if (off == 0)
			memmove(ctl->resetvec.c, buf, sizeof(ctl->resetvec.c));
		while (n & 3)
			buf[n++] = 0;
		sbmem(1, buf, n, ctl->rambase + off);
		if (isconfig)
			break;
		off += n;
	}
	if (Firmwarecmp)
	{
		if (FWDEBUG)
			print("compare...");
		if (!isconfig)
			off = 0;
		for (;;)
		{
			if (!isconfig)
			{
				n = devtab[c->type]->read(c, buf, Uploadsz, off);
				if (n <= 0)
					break;
				while (n & 3)
					buf[n++] = 0;
			}
			sbmem(0, cbuf, n, ctl->rambase + off);
			if (memcmp(buf, cbuf, n) != 0)
			{
				print("ether4330: firmware load failed offset %d\n", off);
				error(Eio);
			}
			if (isconfig)
				break;
			off += n;
		}
	}
	if (FWDEBUG)
		print("\n");
	poperror();
	cclose(c);
	sdfree(buf);
	sdfree(cbuf);
	return n;
}

/*
 * Upload regulatory file (.clm) to firmware.
 * Packet format is
 *	[2]flag [2]type [4]len [4]crc [len]data
 */
static void
reguload(Ctlr *ctl, char *file)
{
	Chan *c;
	uchar *buf;
	int off, n, flag;
	enum
	{
		Reguhdr = 2 + 2 + 4 + 4,
		Regusz = 1400,
		Regutyp = 2,
		Flagclm = 1 << 12,
		Firstpkt = 1 << 1,
		Lastpkt = 1 << 2,
	};

	buf = nil;
	c = findfirmware(file);
	if (waserror())
	{
		cclose(c);
		free(buf);
		nexterror();
	}
	buf = malloc(Reguhdr + Regusz + 1);
	if (buf == nil)
		error(Enomem);
	put2(buf + 2, Regutyp);
	put2(buf + 8, 0);
	off = 0;
	flag = Flagclm | Firstpkt;
	while ((flag & Lastpkt) == 0)
	{
		n = devtab[c->type]->read(c, buf + Reguhdr, Regusz + 1, off);
		if (n <= 0)
			break;
		if (n == Regusz + 1)
			--n;
		else
		{
			while (n & 7)
				buf[Reguhdr + n++] = 0;
			flag |= Lastpkt;
		}
		put2(buf + 0, flag);
		put4(buf + 4, n);
		wlsetvar(ctl, "clmload", buf, Reguhdr + n);
		off += n;
		flag &= ~Firstpkt;
	}
	poperror();
	cclose(c);
	free(buf);
}

static void
fwload(Ctlr *ctl)
{
	uchar buf[4];
	uint i, n;

	i = 0;
	while (firmware[i].chipid != ctl->chipid ||
		   firmware[i].chiprev != ctl->chiprev)
	{
		if (++i == nelem(firmware))
		{
			print("ether4330: no firmware for chipid %x (%d) chiprev %d\n",
				  ctl->chipid, ctl->chipid, ctl->chiprev);
			error("no firmware");
		}
	}
	ctl->regufile = firmware[i].regufile;
	cfgw(Clkcsr, ReqALP);
	while ((cfgr(Clkcsr) & ALPavail) == 0)
		microdelay(10);
	memset(buf, 0, 4);
	sbmem(1, buf, 4, ctl->rambase + ctl->socramsize - 4);
	if (FWDEBUG)
		print("firmware load...");
	upload(ctl, firmware[i].fwfile, 0);
	if (FWDEBUG)
		print("config load...");
	n = upload(ctl, firmware[i].cfgfile, 1);
	n /= 4;
	n = (n & 0xFFFF) | (~n << 16);
	put4(buf, n);
	sbmem(1, buf, 4, ctl->rambase + ctl->socramsize - 4);
	if (ctl->armcore == ARMcr4)
	{
		sbwindow(ctl->sdregs);
		cfgwritel(Fn1, ctl->sdregs + Intstatus, ~0);
		if (ctl->resetvec.i != 0)
		{
			if (SBDEBUG)
				print("%ux\n", ctl->resetvec.i);
			sbmem(1, ctl->resetvec.c, sizeof(ctl->resetvec.c), 0);
		}
		sbreset(ctl->armctl, Cr4Cpuhalt, 0);
	}
	else
		sbreset(ctl->armctl, 0, 0);
}

/*
 * Communication of data and control packets
 */

void intwait(Ctlr *ctlr, int wait)
{
	ulong ints, mbox;
	int i;

	if (waserror())
		return;
	for (;;)
	{
		sdiocardintr(wait);
		sbwindow(ctlr->sdregs);
		i = sdiord(Fn0, Intpend);
		if (i == 0)
		{
			tsleep(&up->sleep, return0, 0, 10);
			continue;
		}
		ints = cfgreadl(Fn1, ctlr->sdregs + Intstatus);
		cfgwritel(Fn1, ctlr->sdregs + Intstatus, ints);
		if (0)
			print("INTS: (%x) %lux -> %lux\n", i, ints, cfgreadl(Fn1, ctlr->sdregs + Intstatus));
		if (ints & MailboxInt)
		{
			mbox = cfgreadl(Fn1, ctlr->sdregs + Hostmboxdata);
			cfgwritel(Fn1, ctlr->sdregs + Sbmbox, 2); /* ack */
			if (mbox & 0x8)
				print("ether4330: firmware ready\n");
		}
		if (ints & FrameInt)
			break;
	}
	poperror();
}

static Block *
wlreadpkt(Ctlr *ctl)
{
	Block *b;
	Sdpcm *p;
	int len, lenck;

	b = allocb(2048);
	p = (Sdpcm *)b->wp;
	qlock(&ctl->pktlock);
	for (;;)
	{
		packetrw(0, b->wp, sizeof(*p));
		len = p->len[0] | p->len[1] << 8;
		if (len == 0)
		{
			freeb(b);
			b = nil;
			break;
		}
		lenck = p->lenck[0] | p->lenck[1] << 8;
		if (lenck != (len ^ 0xFFFF) ||
			len < sizeof(*p) || len > 2048)
		{
			print("ether4330: wlreadpkt error len %.4x lenck %.4x\n", len, lenck);
			cfgw(Framectl, Rfhalt);
			while (cfgr(Rfrmcnt + 1))
				;
			while (cfgr(Rfrmcnt))
				;
			continue;
		}
		if (len > sizeof(*p))
			packetrw(0, b->wp + sizeof(*p), len - sizeof(*p));
		b->wp += len;
		break;
	}
	qunlock(&ctl->pktlock);
	return b;
}

static void
txstart(Ether *edev)
{
	Ctlr *ctl;
	Sdpcm *p;
	Block *b;
	int len, off;

	ctl = edev->ctlr;
	if (!canqlock(&ctl->tlock))
		return;
	if (waserror())
	{
		qunlock(&ctl->tlock);
		return;
	}
	for (;;)
	{
		lock(&ctl->txwinlock);
		if (ctl->txseq == ctl->txwindow)
		{
			//print("f");
			unlock(&ctl->txwinlock);
			break;
		}
		if (ctl->fcmask & 1 << 2)
		{
			//print("x");
			unlock(&ctl->txwinlock);
			break;
		}
		unlock(&ctl->txwinlock);
		b = qget(edev->oq);
		if (b == nil)
			break;
		off = ((uintptr)b->rp & 3) + sizeof(Sdpcm);
		b = padblock(b, off + 4);
		len = BLEN(b);
		p = (Sdpcm *)b->rp;
		memset(p, 0, off); /* TODO: refactor dup code */
		put2(p->len, len);
		put2(p->lenck, ~len);
		p->chanflg = 2;
		p->seq = ctl->txseq;
		p->doffset = off;
		put4(b->rp + off, 0x20); /* BDC header */
		if (iodebug)
			dump("send", b->rp, len);
		qlock(&ctl->pktlock);
		if (waserror())
		{
			if (iodebug)
				print("halt frame %x %x\n", cfgr(Wfrmcnt + 1), cfgr(Wfrmcnt + 1));
			cfgw(Framectl, Wfhalt);
			while (cfgr(Wfrmcnt + 1))
				;
			while (cfgr(Wfrmcnt))
				;
			qunlock(&ctl->pktlock);
			nexterror();
		}
		packetrw(1, b->rp, len);
		ctl->txseq++;
		poperror();
		qunlock(&ctl->pktlock);
		freeb(b);
	}
	poperror();
	qunlock(&ctl->tlock);
}

static void
rproc(void *a)
{
	Ether *edev;
	Ctlr *ctl;
	Block *b;
	Sdpcm *p;
	Cmd *q;
	int flowstart;

	edev = a;
	ctl = edev->ctlr;
	flowstart = 0;
	for (;;)
	{
		if (flowstart)
		{
			//print("F");
			flowstart = 0;
			txstart(edev);
		}
		b = wlreadpkt(ctl);
		if (b == nil)
		{
			intwait(ctl, 1);
			continue;
		}
		p = (Sdpcm *)b->rp;
		if (p->window != ctl->txwindow || p->fcmask != ctl->fcmask)
		{
			lock(&ctl->txwinlock);
			if (p->window != ctl->txwindow)
			{
				if (ctl->txseq == ctl->txwindow)
					flowstart = 1;
				ctl->txwindow = p->window;
			}
			if (p->fcmask != ctl->fcmask)
			{
				if ((p->fcmask & 1 << 2) == 0)
					flowstart = 1;
				ctl->fcmask = p->fcmask;
			}
			unlock(&ctl->txwinlock);
		}
		switch (p->chanflg & 0xF)
		{
		case 0:
			if (iodebug)
				dump("rsp", b->rp, BLEN(b));
			if (BLEN(b) < sizeof(Sdpcm) + sizeof(Cmd))
				break;
			q = (Cmd *)(b->rp + sizeof(*p));
			if ((q->id[0] | q->id[1] << 8) != ctl->reqid)
				break;
			ctl->rsp = b;
			wakeup(&ctl->cmdr);
			continue;
		case 1:
			if (iodebug)
				dump("event", b->rp, BLEN(b));
			if (BLEN(b) > p->doffset + 4)
			{
				b->rp += p->doffset + 4; /* skip BDC header */
				bcmevent(ctl, b->rp, BLEN(b));
			}
			else if (iodebug)
				print("short event %ld %d\n", BLEN(b), p->doffset);
			break;
		case 2:
			if (iodebug)
				dump("packet", b->rp, BLEN(b));
			b->rp += p->doffset + 4; /* skip BDC header */
			if (BLEN(b) < ETHERHDRSIZE)
				break;
			etheriq(edev, b, 1);
			continue;
		default:
			dump("ether4330: bad packet", b->rp, BLEN(b));
			break;
		}
		freeb(b);
	}
}

static void
linkdown(Ctlr *ctl)
{
	Ether *edev;
	Netfile *f;
	int i;

	edev = ctl->edev;
	if (edev == nil || ctl->status != Connected)
		return;
	ctl->status = Disconnected;
	/* send eof to aux/wpa */
	for (i = 0; i < edev->nfile; i++)
	{
		f = edev->f[i];
		if (f == nil || f->in == nil || f->inuse == 0 || f->type != 0x888e)
			continue;
		qwrite(f->in, 0, 0);
	}
}

/*
 * Command interface between host and firmware
 */

static char *eventnames[] = {
	[0] = "set ssid",
	[1] = "join",
	[2] = "start",
	[3] = "auth",
	[4] = "auth ind",
	[5] = "deauth",
	[6] = "deauth ind",
	[7] = "assoc",
	[8] = "assoc ind",
	[9] = "reassoc",
	[10] = "reassoc ind",
	[11] = "disassoc",
	[12] = "disassoc ind",
	[13] = "quiet start",
	[14] = "quiet end",
	[15] = "beacon rx",
	[16] = "link",
	[17] = "mic error",
	[18] = "ndis link",
	[19] = "roam",
	[20] = "txfail",
	[21] = "pmkid cache",
	[22] = "retrograde tsf",
	[23] = "prune",
	[24] = "autoauth",
	[25] = "eapol msg",
	[26] = "scan complete",
	[27] = "addts ind",
	[28] = "delts ind",
	[29] = "bcnsent ind",
	[30] = "bcnrx msg",
	[31] = "bcnlost msg",
	[32] = "roam prep",
	[33] = "pfn net found",
	[34] = "pfn net lost",
	[35] = "reset complete",
	[36] = "join start",
	[37] = "roam start",
	[38] = "assoc start",
	[39] = "ibss assoc",
	[40] = "radio",
	[41] = "psm watchdog",
	[44] = "probreq msg",
	[45] = "scan confirm ind",
	[46] = "psk sup",
	[47] = "country code changed",
	[48] = "exceeded medium time",
	[49] = "icv error",
	[50] = "unicast decode error",
	[51] = "multicast decode error",
	[52] = "trace",
	[53] = "bta hci event",
	[54] = "if",
	[55] = "p2p disc listen complete",
	[56] = "rssi",
	[57] = "pfn scan complete",
	[58] = "extlog msg",
	[59] = "action frame",
	[60] = "action frame complete",
	[61] = "pre assoc ind",
	[62] = "pre reassoc ind",
	[63] = "channel adopted",
	[64] = "ap started",
	[65] = "dfs ap stop",
	[66] = "dfs ap resume",
	[67] = "wai sta event",
	[68] = "wai msg",
	[69] = "escan result",
	[70] = "action frame off chan complete",
	[71] = "probresp msg",
	[72] = "p2p probreq msg",
	[73] = "dcs request",
	[74] = "fifo credit map",
	[75] = "action frame rx",
	[76] = "wake event",
	[77] = "rm complete",
	[78] = "htsfsync",
	[79] = "overlay req",
	[80] = "csa complete ind",
	[81] = "excess pm wake event",
	[82] = "pfn scan none",
	[83] = "pfn scan allgone",
	[84] = "gtk plumbed",
	[85] = "assoc ind ndis",
	[86] = "reassoc ind ndis",
	[87] = "assoc req ie",
	[88] = "assoc resp ie",
	[89] = "assoc recreated",
	[90] = "action frame rx ndis",
	[91] = "auth req",
	[92] = "tdls peer event",
	[127] = "bcmc credit support"};

static char *
evstring(uint event)
{
	static char buf[12];

	if (event >= nelem(eventnames) || eventnames[event] == 0)
	{
		/* not reentrant but only called from one kproc */
		snprint(buf, sizeof buf, "%d", event);
		return buf;
	}
	return eventnames[event];
}

static void
bcmevent(Ctlr *ctl, uchar *p, int len)
{
	int flags;
	long event, status, reason;

	if (len < ETHERHDRSIZE + 10 + 46)
		return;
	p += ETHERHDRSIZE + 10; /* skip bcm_ether header */
	len -= ETHERHDRSIZE + 10;
	flags = nhgets(p + 2);
	event = nhgets(p + 6);
	status = nhgetl(p + 8);
	reason = nhgetl(p + 12);
	if (EVENTDEBUG)
		print("ether4330: [%s] status %ld flags %#x reason %ld\n",
			  evstring(event), status, flags, reason);
	switch (event)
	{
	case 19: /* E_ROAM */
		if (status == 0)
			break;
	/* fall through */
	case 0: /* E_SET_SSID */
		ctl->joinstatus = 1 + status;
		wakeup(&ctl->joinr);
		break;
	case 16:		   /* E_LINK */
		if (flags & 1) /* link up */
			break;
	/* fall through */
	case 5:  /* E_DEAUTH */
	case 6:  /* E_DEAUTH_IND */
	case 12: /* E_DISASSOC_IND */
		linkdown(ctl);
		break;
	case 26: /* E_SCAN_COMPLETE */
		break;
	case 69: /* E_ESCAN_RESULT */
		wlscanresult(ctl->edev, p + 48, len - 48);
		break;
	default:
		if (status)
		{
			if (!EVENTDEBUG)
				print("ether4330: [%s] error status %ld flags %#x reason %ld\n",
					  evstring(event), status, flags, reason);
			dump("event", p, len);
		}
	}
}

static int
joindone(void *a)
{
	return ((Ctlr *)a)->joinstatus;
}

static int
waitjoin(Ctlr *ctl)
{
	int n;

	sleep(&ctl->joinr, joindone, ctl);
	n = ctl->joinstatus;
	ctl->joinstatus = 0;
	return n - 1;
}

static int
cmddone(void *a)
{
	return ((Ctlr *)a)->rsp != nil;
}

static void
wlcmd(Ctlr *ctl, int write, int op, void *data, int dlen, void *res, int rlen)
{
	Block *b;
	Sdpcm *p;
	Cmd *q;
	int len, tlen;

	if (write)
		tlen = dlen + rlen;
	else
		tlen = MAX(dlen, rlen);
	len = sizeof(Sdpcm) + sizeof(Cmd) + tlen;
	b = allocb(len);
	qlock(&ctl->cmdlock);
	if (waserror())
	{
		freeb(b);
		qunlock(&ctl->cmdlock);
		nexterror();
	}
	memset(b->wp, 0, len);
	qlock(&ctl->pktlock);
	p = (Sdpcm *)b->wp;
	put2(p->len, len);
	put2(p->lenck, ~len);
	p->seq = ctl->txseq;
	p->doffset = sizeof(Sdpcm);
	b->wp += sizeof(*p);

	q = (Cmd *)b->wp;
	put4(q->cmd, op);
	put4(q->len, tlen);
	put2(q->flags, write ? 2 : 0);
	put2(q->id, ++ctl->reqid);
	put4(q->status, 0);
	b->wp += sizeof(*q);

	if (dlen > 0)
		memmove(b->wp, data, dlen);
	if (write)
		memmove(b->wp + dlen, res, rlen);
	b->wp += tlen;

	if (iodebug)
		dump("cmd", b->rp, len);
	packetrw(1, b->rp, len);
	ctl->txseq++;
	qunlock(&ctl->pktlock);
	freeb(b);
	b = nil;
	USED(b);
	sleep(&ctl->cmdr, cmddone, ctl);
	b = ctl->rsp;
	ctl->rsp = nil;
	assert(b != nil);
	p = (Sdpcm *)b->rp;
	q = (Cmd *)(b->rp + p->doffset);
	if (q->status[0] | q->status[1] | q->status[2] | q->status[3])
	{
		print("ether4330: cmd %d error status %ld\n", op, get4(q->status));
		dump("ether4330: cmd error", b->rp, BLEN(b));
		error("wlcmd error");
	}
	if (!write)
		memmove(res, q + 1, rlen);
	freeb(b);
	qunlock(&ctl->cmdlock);
	poperror();
}

static void
wlcmdint(Ctlr *ctl, int op, int val)
{
	uchar buf[4];

	put4(buf, val);
	wlcmd(ctl, 1, op, buf, 4, nil, 0);
}

static void
wlgetvar(Ctlr *ctl, char *name, void *val, int len)
{
	wlcmd(ctl, 0, GetVar, name, strlen(name) + 1, val, len);
}

static void
wlsetvar(Ctlr *ctl, char *name, void *val, int len)
{
	if (VARDEBUG)
	{
		char buf[32];
		snprint(buf, sizeof buf, "wlsetvar %s:", name);
		dump(buf, val, len);
	}
	wlcmd(ctl, 1, SetVar, name, strlen(name) + 1, val, len);
}

static void
wlsetint(Ctlr *ctl, char *name, int val)
{
	uchar buf[4];

	put4(buf, val);
	wlsetvar(ctl, name, buf, 4);
}

static void
wlwepkey(Ctlr *ctl, int i)
{
	uchar params[164];
	uchar *p;

	memset(params, 0, sizeof params);
	p = params;
	p = put4(p, i); /* index */
	p = put4(p, ctl->keys[i].len);
	memmove(p, ctl->keys[i].dat, ctl->keys[i].len);
	p += 32 + 18 * 4; /* keydata, pad */
	if (ctl->keys[i].len == WMinKeyLen)
		p = put4(p, 1); /* algo = WEP1 */
	else
		p = put4(p, 3); /* algo = WEP128 */
	put4(p, 2);			/* flags = Primarykey */

	wlsetvar(ctl, "wsec_key", params, sizeof params);
}

static void
memreverse(char *dst, char *src, int len)
{
	src += len;
	while (len-- > 0)
		*dst++ = *--src;
}

static void
wlwpakey(Ctlr *ctl, int id, uvlong iv, uchar *ea)
{
	uchar params[164];
	uchar *p;
	int pairwise;

	if (id == CMrxkey)
		return;
	pairwise = (id == CMrxkey || id == CMtxkey);
	memset(params, 0, sizeof params);
	p = params;
	if (pairwise)
		p = put4(p, 0);
	else
		p = put4(p, id - CMrxkey0); /* group key id */
	p = put4(p, ctl->keys[0].len);
	memmove((char *)p, ctl->keys[0].dat, ctl->keys[0].len);
	p += 32 + 18 * 4; /* keydata, pad */
	if (ctl->cryptotype == Wpa)
		p = put4(p, 2); /* algo = TKIP */
	else
		p = put4(p, 4); /* algo = AES_CCM */
	if (pairwise)
		p = put4(p, 0);
	else
		p = put4(p, 2); /* flags = Primarykey */
	p += 3 * 4;
	p = put4(p, 0); //pairwise);		/* iv initialised */
	p += 4;
	p = put4(p, iv >> 16);	/* iv high */
	p = put2(p, iv & 0xFFFF); /* iv low */
	p += 2 + 2 * 4;			  /* align, pad */
	if (pairwise)
		memmove(p, ea, Eaddrlen);

	wlsetvar(ctl, "wsec_key", params, sizeof params);
}

static void
wljoin(Ctlr *ctl, char *ssid, int chan)
{
	uchar params[72];
	uchar *p;
	int n;

	if (chan != 0)
		chan |= 0x2b00; /* 20Mhz channel width */
	p = params;
	n = strlen(ssid);
	n = MIN(n, 32);
	p = put4(p, n);
	memmove(p, ssid, n);
	memset(p + n, 0, 32 - n);
	p += 32;
	p = put4(p, 0xff); /* scan type */
	if (chan != 0)
	{
		p = put4(p, 2);   /* num probes */
		p = put4(p, 120); /* active time */
		p = put4(p, 390); /* passive time */
	}
	else
	{
		p = put4(p, -1); /* num probes */
		p = put4(p, -1); /* active time */
		p = put4(p, -1); /* passive time */
	}
	p = put4(p, -1);		   /* home time */
	memset(p, 0xFF, Eaddrlen); /* bssid */
	p += Eaddrlen;
	p = put2(p, 0); /* pad */
	if (chan != 0)
	{
		p = put4(p, 1);	/* num chans */
		p = put2(p, chan); /* chan spec */
		p = put2(p, 0);	/* pad */
		assert(p == params + sizeof(params));
	}
	else
	{
		p = put4(p, 0); /* num chans */
		assert(p == params + sizeof(params) - 4);
	}

	wlsetvar(ctl, "join", params, chan ? sizeof params : sizeof params - 4);
	ctl->status = Connecting;
	switch (waitjoin(ctl))
	{
	case 0:
		ctl->status = Connected;
		break;
	case 3:
		ctl->status = Disconnected;
		error("wifi join: network not found");
	case 1:
		ctl->status = Disconnected;
		error("wifi join: failed");
	default:
		ctl->status = Disconnected;
		error("wifi join: error");
	}
}

static void
wlscanstart(Ctlr *ctl)
{
	/* version[4] action[2] sync_id[2] ssidlen[4] ssid[32] bssid[6] bss_type[1]
		scan_type[1] nprobes[4] active_time[4] passive_time[4] home_time[4]
		nchans[2] nssids[2] chans[nchans][2] ssids[nssids][32] */
	/* hack - this is only correct on a little-endian cpu */
	static uchar params[4 + 2 + 2 + 4 + 32 + 6 + 1 + 1 + 4 * 4 + 2 + 2 + 14 * 2 + 32 + 4] = {
		1,
		0,
		0,
		0,
		1,
		0,
		0x34,
		0x12,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		2,
		0,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		14,
		0,
		1,
		0,
		0x01,
		0x2b,
		0x02,
		0x2b,
		0x03,
		0x2b,
		0x04,
		0x2b,
		0x05,
		0x2e,
		0x06,
		0x2e,
		0x07,
		0x2e,
		0x08,
		0x2b,
		0x09,
		0x2b,
		0x0a,
		0x2b,
		0x0b,
		0x2b,
		0x0c,
		0x2b,
		0x0d,
		0x2b,
		0x0e,
		0x2b,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
	};

	wlcmdint(ctl, 49, 0); /* PASSIVE_SCAN */
	wlsetvar(ctl, "escan", params, sizeof params);
}

static uchar *
gettlv(uchar *p, uchar *ep, int tag)
{
	int len;

	while (p + 1 < ep)
	{
		len = p[1];
		if (p + 2 + len > ep)
			return nil;
		if (p[0] == tag)
			return p;
		p += 2 + len;
	}
	return nil;
}

static void
addscan(Block *bp, uchar *p, int len)
{
	char bssid[24];
	char *auth, *auth2;
	uchar *t, *et;
	int ielen;
	static uchar wpaie1[4] = {0x00, 0x50, 0xf2, 0x01};

	snprint(bssid, sizeof bssid, ";bssid=%E", p + 8);
	if (strstr((char *)bp->rp, bssid) != nil)
		return;
	bp->wp = (uchar *)seprint((char *)bp->wp, (char *)bp->lim,
							  "ssid=%.*s%s;signal=%d;noise=%d;chan=%d",
							  p[18], (char *)p + 19, bssid,
							  (short)get2(p + 78), (signed char)p[80],
							  get2(p + 72) & 0xF);
	auth = auth2 = "";
	if (get2(p + 16) & 0x10)
		auth = ";wep";
	ielen = get4(p + 0x78);
	if (ielen > 0)
	{
		t = p + get4(p + 0x74);
		et = t + ielen;
		if (et > p + len)
			return;
		if (gettlv(t, et, 0x30) != nil)
		{
			auth = "";
			auth2 = ";wpa2";
		}
		while ((t = gettlv(t, et, 0xdd)) != nil)
		{
			if (t[1] > 4 && memcmp(t + 2, wpaie1, 4) == 0)
			{
				auth = ";wpa";
				break;
			}
			t += 2 + t[1];
		}
	}
	bp->wp = (uchar *)seprint((char *)bp->wp, (char *)bp->lim,
							  "%s%s\n", auth, auth2);
}

static void
wlscanresult(Ether *edev, uchar *p, int len)
{
	Ctlr *ctlr;
	Netfile **ep, *f, **fp;
	Block *bp;
	int nbss, i;

	ctlr = edev->ctlr;
	if (get4(p) > len)
		return;
	/* TODO: more syntax checking */
	bp = ctlr->scanb;
	if (bp == nil)
		ctlr->scanb = bp = allocb(8192);
	nbss = get2(p + 10);
	p += 12;
	len -= 12;
	if (0)
		dump("SCAN", p, len);
	if (nbss)
	{
		addscan(bp, p, len);
		return;
	}
	i = edev->scan;
	ep = &edev->f[Ntypes];
	for (fp = edev->f; fp < ep && i > 0; fp++)
	{
		f = *fp;
		if (f == nil || f->scan == 0)
			continue;
		if (i == 1)
			qpass(f->in, bp);
		else
			qpass(f->in, copyblock(bp, BLEN(bp)));
		i--;
	}
	if (i)
		freeb(bp);
	ctlr->scanb = nil;
}

static void
lproc(void *a)
{
	Ether *edev;
	Ctlr *ctlr;
	int secs;

	edev = a;
	ctlr = edev->ctlr;
	secs = 0;
	for (;;)
	{
		tsleep(&up->sleep, return0, 0, 1000);
		if (ctlr->scansecs)
		{
			if (secs == 0)
			{
				if (waserror())
					ctlr->scansecs = 0;
				else
				{
					wlscanstart(ctlr);
					poperror();
				}
				secs = ctlr->scansecs;
			}
			--secs;
		}
		else
			secs = 0;
	}
}

static void
wlinit(Ether *edev, Ctlr *ctlr)
{
	uchar ea[Eaddrlen];
	uchar eventmask[16];
	char version[128];
	char *p;
	static uchar keepalive[12] = {1, 0, 11, 0, 0xd8, 0xd6, 0, 0, 0, 0, 0, 0};

	wlgetvar(ctlr, "cur_etheraddr", ea, Eaddrlen);
	memmove(edev->ea, ea, Eaddrlen);
	memmove(edev->addr, ea, Eaddrlen);
	print("ether4330: addr %E\n", edev->ea);
	wlsetint(ctlr, "assoc_listen", 10);
	if (ctlr->chipid == 43430 || ctlr->chipid == 0x4345)
		wlcmdint(ctlr, 0x56, 0); /* powersave off */
	else
		wlcmdint(ctlr, 0x56, 2); /* powersave FAST */
	wlsetint(ctlr, "bus:txglom", 0);
	wlsetint(ctlr, "bcn_timeout", 10);
	wlsetint(ctlr, "assoc_retry_max", 3);
	if (ctlr->chipid == 0x4330)
	{
		wlsetint(ctlr, "btc_wire", 4);
		wlsetint(ctlr, "btc_mode", 1);
		wlsetvar(ctlr, "mkeep_alive", keepalive, 11);
	}
	memset(eventmask, 0xFF, sizeof eventmask);
#define ENABLE(n) eventmask[n / 8] |= 1 << (n % 8)
#define DISABLE(n) eventmask[n / 8] &= ~(1 << (n % 8))
	DISABLE(40);  /* E_RADIO */
	DISABLE(44);  /* E_PROBREQ_MSG */
	DISABLE(54);  /* E_IF */
	DISABLE(71);  /* E_PROBRESP_MSG */
	DISABLE(20);  /* E_TXFAIL */
	DISABLE(124); /* ? */
	wlsetvar(ctlr, "event_msgs", eventmask, sizeof eventmask);
	wlcmdint(ctlr, 0xb9, 0x28);  /* SET_SCAN_CHANNEL_TIME */
	wlcmdint(ctlr, 0xbb, 0x28);  /* SET_SCAN_UNASSOC_TIME */
	wlcmdint(ctlr, 0x102, 0x82); /* SET_SCAN_PASSIVE_TIME */
	wlcmdint(ctlr, 2, 0);		 /* UP */
	memset(version, 0, sizeof version);
	wlgetvar(ctlr, "ver", version, sizeof version - 1);
	if ((p = strchr(version, '\n')) != nil)
		*p = '\0';
	if (0)
		print("ether4330: %s\n", version);
	wlsetint(ctlr, "roam_off", 1);
	wlcmdint(ctlr, 0x14, 1); /* SET_INFRA 1 */
	wlcmdint(ctlr, 10, 0);   /* SET_PROMISC */
	//wlcmdint(ctlr, 0x8e, 0);	/* SET_BAND 0 */
	//wlsetint(ctlr, "wsec", 1);
	wlcmdint(ctlr, 2, 1); /* UP */
	ctlr->keys[0].len = WMinKeyLen;
	//wlwepkey(ctlr, 0);
}

/*
 * Plan 9 driver interface
 */

static long
etherbcmifstat(Ether *edev, void *a, long n, ulong offset)
{
	Ctlr *ctlr;
	char *p;
	int l;
	static char *cryptoname[4] = {
		[0] "off",
		[Wep] "wep",
		[Wpa] "wpa",
		[Wpa2] "wpa2",
	};
	/* these strings are known by aux/wpa */
	static char *connectstate[] = {
		[Disconnected] = "unassociated",
		[Connecting] = "connecting",
		[Connected] = "associated",
	};

	ctlr = edev->ctlr;
	if (ctlr == nil)
		return 0;
	p = malloc(READSTR);
	l = 0;

	l += snprint(p + l, READSTR - l, "channel: %d\n", ctlr->chanid);
	l += snprint(p + l, READSTR - l, "essid: %s\n", ctlr->essid);
	l += snprint(p + l, READSTR - l, "crypt: %s\n", cryptoname[ctlr->cryptotype]);
	l += snprint(p + l, READSTR - l, "oq: %d\n", qlen(edev->oq));
	l += snprint(p + l, READSTR - l, "txwin: %d\n", ctlr->txwindow);
	l += snprint(p + l, READSTR - l, "txseq: %d\n", ctlr->txseq);
	l += snprint(p + l, READSTR - l, "status: %s\n", connectstate[ctlr->status]);
	USED(l);
	n = readstr(offset, a, n, p);
	free(p);
	return n;
}

static void
etherbcmtransmit(Ether *edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if (ctlr == nil)
		return;
	txstart(edev);
}

static int
parsehex(char *buf, int buflen, char *a)
{
	int i, k, n;

	k = 0;
	for (i = 0; k < buflen && *a; i++)
	{
		if (*a >= '0' && *a <= '9')
			n = *a++ - '0';
		else if (*a >= 'a' && *a <= 'f')
			n = *a++ - 'a' + 10;
		else if (*a >= 'A' && *a <= 'F')
			n = *a++ - 'A' + 10;
		else
			break;

		if (i & 1)
		{
			buf[k] |= n;
			k++;
		}
		else
			buf[k] = n << 4;
	}
	if (i & 1)
		return -1;
	return k;
}

static int
wepparsekey(WKey *key, char *a)
{
	int i, k, len, n;
	char buf[WMaxKeyLen];

	len = strlen(a);
	if (len == WMinKeyLen || len == WMaxKeyLen)
	{
		memset(key->dat, 0, sizeof(key->dat));
		memmove(key->dat, a, len);
		key->len = len;

		return 0;
	}
	else if (len == WMinKeyLen * 2 || len == WMaxKeyLen * 2)
	{
		k = 0;
		for (i = 0; i < len; i++)
		{
			if (*a >= '0' && *a <= '9')
				n = *a++ - '0';
			else if (*a >= 'a' && *a <= 'f')
				n = *a++ - 'a' + 10;
			else if (*a >= 'A' && *a <= 'F')
				n = *a++ - 'A' + 10;
			else
				return -1;

			if (i & 1)
			{
				buf[k] |= n;
				k++;
			}
			else
				buf[k] = n << 4;
		}

		memset(key->dat, 0, sizeof(key->dat));
		memmove(key->dat, buf, k);
		key->len = k;

		return 0;
	}

	return -1;
}

static int
wpaparsekey(WKey *key, uvlong *ivp, char *a)
{
	int len;
	char *e;

	if (cistrncmp(a, "tkip:", 5) == 0 || cistrncmp(a, "ccmp:", 5) == 0)
		a += 5;
	else
		return 1;
	len = parsehex(key->dat, sizeof(key->dat), a);
	if (len <= 0)
		return 1;
	key->len = len;
	a += 2 * len;
	if (*a++ != '@')
		return 1;
	*ivp = strtoull(a, &e, 16);
	if (e == a)
		return -1;
	return 0;
}

static void
setauth(Ctlr *ctlr, Cmdbuf *cb, char *a)
{
	uchar wpaie[32];
	int i;

	i = parsehex((char *)wpaie, sizeof wpaie, a);
	if (i < 2 || i != wpaie[1] + 2)
		cmderror(cb, "bad wpa ie syntax");
	if (wpaie[0] == 0xdd)
		ctlr->cryptotype = Wpa;
	else if (wpaie[0] == 0x30)
		ctlr->cryptotype = Wpa2;
	else
		cmderror(cb, "bad wpa ie");
	wlsetvar(ctlr, "wpaie", wpaie, i);
	if (ctlr->cryptotype == Wpa)
	{
		wlsetint(ctlr, "wpa_auth", 4 | 2); /* auth_psk | auth_unspecified */
		wlsetint(ctlr, "auth", 0);
		wlsetint(ctlr, "wsec", 2);	 /* tkip */
		wlsetint(ctlr, "wpa_auth", 4); /* auth_psk */
	}
	else
	{
		wlsetint(ctlr, "wpa_auth", 0x80 | 0x40); /* auth_psk | auth_unspecified */
		wlsetint(ctlr, "auth", 0);
		wlsetint(ctlr, "wsec", 4);		  /* aes */
		wlsetint(ctlr, "wpa_auth", 0x80); /* auth_psk */
	}
}

static int
setcrypt(Ctlr *ctlr, Cmdbuf *, char *a)
{
	if (cistrcmp(a, "wep") == 0 || cistrcmp(a, "on") == 0)
		ctlr->cryptotype = Wep;
	else if (cistrcmp(a, "off") == 0 || cistrcmp(a, "none") == 0)
		ctlr->cryptotype = 0;
	else
		return 0;
	wlsetint(ctlr, "auth", ctlr->cryptotype);
	return 1;
}

static long
etherbcmctl(Ether *edev, void *buf, long n)
{
	Ctlr *ctlr;
	Cmdbuf *cb;
	Cmdtab *ct;
	uchar ea[Eaddrlen];
	uvlong iv;
	int i;

	if ((ctlr = edev->ctlr) == nil)
		error(Enonexist);
	USED(ctlr);

	cb = parsecmd(buf, n);
	if (waserror())
	{
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, cmds, nelem(cmds));
	switch (ct->index)
	{
	case CMauth:
		setauth(ctlr, cb, cb->f[1]);
		if (ctlr->essid[0])
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
		break;
	case CMchannel:
		if ((i = atoi(cb->f[1])) < 0 || i > 16)
			cmderror(cb, "bad channel number");
		//wlcmdint(ctlr, 30, i);	/* SET_CHANNEL */
		ctlr->chanid = i;
		break;
	case CMcrypt:
		if (setcrypt(ctlr, cb, cb->f[1]))
		{
			if (ctlr->essid[0])
				wljoin(ctlr, ctlr->essid, ctlr->chanid);
		}
		else
			cmderror(cb, "bad crypt type");
		break;
	case CMessid:
		if (cistrcmp(cb->f[1], "default") == 0)
			memset(ctlr->essid, 0, sizeof(ctlr->essid));
		else
		{
			strncpy(ctlr->essid, cb->f[1], sizeof(ctlr->essid) - 1);
			ctlr->essid[sizeof(ctlr->essid) - 1] = '\0';
		}
		if (!waserror())
		{
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
			poperror();
		}
		break;
	case CMjoin: /* join essid channel wep|on|off|wpakey */
		if (strcmp(cb->f[1], "") != 0)
		{ /* empty string for no change */
			if (cistrcmp(cb->f[1], "default") != 0)
			{
				strncpy(ctlr->essid, cb->f[1], sizeof(ctlr->essid) - 1);
				ctlr->essid[sizeof(ctlr->essid) - 1] = 0;
			}
			else
				memset(ctlr->essid, 0, sizeof(ctlr->essid));
		}
		else if (ctlr->essid[0] == 0)
			cmderror(cb, "essid not set");
		if ((i = atoi(cb->f[2])) >= 0 && i <= 16)
			ctlr->chanid = i;
		else
			cmderror(cb, "bad channel number");
		if (!setcrypt(ctlr, cb, cb->f[3]))
			setauth(ctlr, cb, cb->f[3]);
		if (ctlr->essid[0])
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
		break;
	case CMkey1:
	case CMkey2:
	case CMkey3:
	case CMkey4:
		i = ct->index - CMkey1;
		if (wepparsekey(&ctlr->keys[i], cb->f[1]))
			cmderror(cb, "bad WEP key syntax");
		wlsetint(ctlr, "wsec", 1); /* wep enabled */
		wlwepkey(ctlr, i);
		break;
	case CMrxkey:
	case CMrxkey0:
	case CMrxkey1:
	case CMrxkey2:
	case CMrxkey3:
	case CMtxkey:
		if (parseether(ea, cb->f[1]) < 0)
			cmderror(cb, "bad ether addr");
		if (wpaparsekey(&ctlr->keys[0], &iv, cb->f[2]))
			cmderror(cb, "bad wpa key");
		wlwpakey(ctlr, ct->index, iv, ea);
		break;
	case CMdebug:
		iodebug = atoi(cb->f[1]);
		break;
	}
	poperror();
	free(cb);
	return n;
}

static void
etherbcmscan(void *a, uint secs)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = a;
	ctlr = edev->ctlr;
	ctlr->scansecs = secs;
}

static void
etherbcmattach(Ether *edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	qlock(&ctlr->alock);
	if (waserror())
	{
		//print("ether4330: attach failed: %s\n", up->errstr);
		qunlock(&ctlr->alock);
		nexterror();
	}
	if (ctlr->edev == nil)
	{
		if (ctlr->chipid == 0)
		{
			sdioinit();
			sbinit(ctlr);
		}
		fwload(ctlr);
		sbenable(ctlr);
		kproc("wifireader", rproc, edev);
		kproc("wifitimer", lproc, edev);
		if (ctlr->regufile)
			reguload(ctlr, ctlr->regufile);
		wlinit(edev, ctlr);
		ctlr->edev = edev;
	}
	qunlock(&ctlr->alock);
	poperror();
}

static void
etherbcmshutdown(Ether *)
{
	sdioreset();
}

static int
etherbcmpnp(Ether *edev)
{
	Ctlr *ctlr;

	ctlr = malloc(sizeof(Ctlr));
	ctlr->chanid = Wifichan;
	edev->ctlr = ctlr;
	edev->attach = etherbcmattach;
	edev->transmit = etherbcmtransmit;
	edev->ifstat = etherbcmifstat;
	edev->ctl = etherbcmctl;
	edev->scanbs = etherbcmscan;
	edev->shutdown = etherbcmshutdown;
	edev->arg = edev;

	return 0;
}

void ether4330link(void)
{
	addethercard("4330", etherbcmpnp);
}
char*)seprint((char*)bp->wp, (char*)bp->liethergenet.c                                                                                           644       0       0        46521 13527742125  11546                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Broadcom 2711 native gigabit ethernet
 *
 *	from 9front
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "etherif.h"
#include "ethermii.h"

enum
{
	Rbsz		= 2048,
	Maxtu		= 1536,

	DmaOWN		= 0x8000,
	DmaSOP		= 0x2000,
	DmaEOP		= 0x4000,
	DmaRxLg		= 0x10,
	DmaRxNo		= 0x08,
	DmaRxErr	= 0x04,
	DmaRxCrc	= 0x02,
	DmaRxOv		= 0x01,
	DmaRxErrors	= DmaRxLg|DmaRxNo|DmaRxErr|DmaRxCrc|DmaRxOv,

	DmaTxQtag	= 0x1F80,
	DmaTxUnderrun	= 0x0200,
	DmaTxAppendCrc	= 0x0040,
	DmaTxOwCrc	= 0x0020,
	DmaTxDoCsum	= 0x0010,

	/* Ctlr->regs */
	SysRevision	= 0x00/4,
	SysPortCtrl	= 0x04/4,
		PortModeIntEphy	= 0,
		PortModeIntGphy = 1,
		PortModeExtEphy = 2,
		PortModeExtGphy = 3,
		PortModeExtRvmii50 = 4,
		PortModeExtRvmii25 = 16 | 4,
		LedActSourceMac = 1 << 9,

	SysRbufFlushCtrl	= 0x08/4,
	SysTbufFlushCtrl	= 0x0C/4,

	ExtRgmiiOobCtrl	= 0x8C/4,
		RgmiiLink	= 1 << 4,
		OobDisable	= 1 << 5,
		RgmiiModeEn	= 1 << 6,
		IdModeDis	= 1 << 16,

	Intrl0		= 0x200/4,
		IrqScb		= 1 << 0,
		IrqEphy		= 1 << 1,
		IrqPhyDetR	= 1 << 2,
		IrqPhyDetF	= 1 << 3,
		IrqLinkUp	= 1 << 4,
		IrqLinkDown	= 1 << 5,
		IrqUmac		= 1 << 6,
		IrqUmacTsv	= 1 << 7,
		IrqTbufUnderrun	= 1 << 8,
		IrqRbufOverflow	= 1 << 9,
		IrqHfbSm	= 1 << 10,
		IrqHfbMm	= 1 << 11,
		IrqMpdR		= 1 << 12,
		IrqRxDmaDone	= 1 << 13,
		IrqRxDmaPDone	= 1 << 14,
		IrqRxDmaBDone	= 1 << 15,
		IrqTxDmaDone	= 1 << 16,
		IrqTxDmaPDone	= 1 << 17,
		IrqTxDmaBDone	= 1 << 18,
		IrqMdioDone	= 1 << 23,
		IrqMdioError	= 1 << 24,
	Intrl1		= 0x240/4,
		/* Intrl0/1 + ... */
		IntrSts		= 0x00/4,
		IntrSet		= 0x04/4,
		IntrClr		= 0x08/4,
		IntrMaskSts	= 0x0C/4,
		IntrMaskSet	= 0x10/4,
		IntrMaskClr	= 0x14/4,

	RbufCtrl	= 0x300/4,
		Rbuf64En	= 1 << 0,
		RbufAlign2B	= 1 << 1,
		RbufBadDis	= 1 << 2,

	RbufChkCtrl	= 0x314/4,
		RbufChkRxChkEn	= 1 << 0,
		RbufChkSkipFcs	= 1 << 4,

	RbufOvflCnt	= 0x394/4,
	RbufErrCnt	= 0x398/4,

	RbufEnergyCtrl	= 0x39c/4,
		RbufEeeEn	= 1 << 0,
		RbufPmEn	= 1 << 1,

	RbufTbufSizeCtrl= 0x3b4/4,

	TbufCtrl	= 0x600/4,
	TbufBpMc	= 0x60C/4,
	TbufEnergyCtrl	= 0x614/4,

	UmacCmd		= 0x808/4,
		CmdTxEn		= 1 << 0,
		CmdRxEn		= 1 << 1,
		CmdSpeed10	= 0 << 2,
		CmdSpeed100	= 1 << 2,
		CmdSpeed1000	= 2 << 2,
		CmdSpeedMask	= 3 << 2,
		CmdProm		= 1 << 4,
		CmdPadEn	= 1 << 5,
		CmdCrcFwd	= 1 << 6,
		CmdPauseFwd	= 1 << 7,
		CmdRxPauseIgn	= 1 << 8,
		CmdTxAddrIn	= 1 << 9,
		CmdHdEn		= 1 << 10,
		CmdSwReset	= 1 << 13,
		CmdLclLoopEn	= 1 << 15,
		CmdAutoConfig	= 1 << 22,
		CmdCntlFrmEn	= 1 << 23,
		CmdNoLenChk	= 1 << 24,
		CmdRmtLoopEn	= 1 << 25,
		CmdPrblEn	= 1 << 27,
		CmdTxPauseIgn	= 1 << 28,
		CmdTxRxEn	= 1 << 29,
		CmdRuntFilterDis= 1 << 30,

	UmacMac0	= 0x80C/4,
	UmacMac1	= 0x810/4,
	UmacMaxFrameLen	= 0x814/4,

	UmacEeeCtrl	= 0x864/4,	
		UmacEeeEn	= 1<<3,

	UmacEeeLpiTimer	= 0x868/4,
	UmacEeeWakeTimer= 0x86C/4,
	UmacEeeRefCount	= 0x870/4,
		EeeRefCountMask = 0xFFFF,

	UmacTxFlush	= 0xb34/4,

	UmacMibCtrl	= 0xd80/4,
		MibResetRx	= 1 << 0,
		MibResetRunt	= 1 << 1,
		MibResetTx	= 1 << 2,

	MdioCmd		= 0xe14/4,
		MdioStartBusy	= 1 << 29,
		MdioReadFail	= 1 << 28,
		MdioRead	= 2 << 26,
		MdioWrite	= 1 << 26,
		MdioPhyShift	= 21,
		MdioPhyMask	= 0x1F,
		MdioAddrShift	= 16,
		MdioAddrMask	= 0x1F,

	UmacMpdCtrl	= 0xe20/4,
		MpdEn	= 1 << 0,
		MpdPwEn	= 1 << 27,

	UmacMdfCtrl	= 0xe50/4,
	UmacMdfAddr0	= 0xe54/4,

	RdmaOffset	= 0x2000/4,
	TdmaOffset	= 0x4000/4,
	HfbOffset	= 0x8000/4,

	HfbCtlr		= 0xFC00/4,
	HfbFltEnable	= 0xFC04/4,
	HfbFltLen	= 0xFC1C/4,

	/* common Ring->regs */
	RdmaWP		= 0x00/4,
	TdmaRP		= 0x00/4,
	RxWP		= 0x08/4,
	TxRP		= 0x08/4,
	TxWP		= 0x0C/4,
	RxRP		= 0x0C/4,
	DmaRingBufSize	= 0x10/4,
	DmaStart	= 0x14/4,
	DmaEnd		= 0x1C/4,
	DmaDoneThresh	= 0x24/4,
	TdmaFlowPeriod	= 0x28/4,
	RdmaXonXoffThresh=0x28/4,
	TdmaWP		= 0x2C/4,
	RdmaRP		= 0x2C/4,

	/*
	 * reg offsets only for RING16
	 * ctlr->rx->regs / ctlr->tx->regs
	 */
	RingCfg		= 0x40/4,
		RxRingCfgMask	= 0x10000,
		TxRingCfgMask	= 0x1000F,

	DmaCtrl		= 0x44/4,
		DmaCtrlEn	= 1 << 0,
	DmaStatus	= 0x48/4,
		DmaStatusDis	= 1 << 0,
	DmaScbBurstSize	= 0x4C/4,

	TdmaArbCtrl	= 0x6C/4,
	TdmaPriority0	= 0x70/4,
	TdmaPriority1	= 0x74/4,
	TdmaPriority2	= 0x78/4,

	RdmaTimeout0	= 0x6C/4,
	RdmaIndex2Ring0	= 0xB0/4,
};

typedef struct Desc Desc;
typedef struct Ring Ring;
typedef struct Ctlr Ctlr;

struct Desc
{
	u32int *d; /* hw descriptor */
	Block *b;
};

struct Ring
{
	Rendez;
	u32int *regs;
	u32int *intregs;
	u32int intmask;

	Desc *d;

	u32int m;
	u32int cp;
	u32int rp;
	u32int wp;

	int num;
};

struct Ctlr
{
	Lock;
	u32int *regs;

	Desc rd[256];
	Desc td[256];

	Ring rx[1 + 0];
	Ring tx[1 + 0];

	Rendez avail[1];
	Rendez link[1];
	struct
	{
		Mii;
		Rendez;
	} mii[1];

	QLock;
	char attached;
};

static Block *scratch;

#define REG(x) (x)

static void
interrupt0(Ureg*, void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	u32int sts;

	sts = REG(ctlr->regs[Intrl0 + IntrSts]) & ~REG(ctlr->regs[Intrl0 + IntrMaskSts]);
	REG(ctlr->regs[Intrl0 + IntrClr]) = sts;
	REG(ctlr->regs[Intrl0 + IntrMaskSet]) = sts;

	if (sts & ctlr->rx->intmask)
		wakeup(ctlr->rx);
	if (sts & ctlr->tx->intmask)
		wakeup(ctlr->tx);

	if (sts & (IrqMdioDone | IrqMdioError))
		wakeup(ctlr->mii);
	if (sts & (IrqLinkUp | IrqLinkDown))
		wakeup(ctlr->link);
}

static void
interrupt1(Ureg*, void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	u32int sts;
	int i;

	sts = REG(ctlr->regs[Intrl1 + IntrSts]) & ~REG(ctlr->regs[Intrl1 + IntrMaskSts]);
	REG(ctlr->regs[Intrl1 + IntrClr]) = sts;
	REG(ctlr->regs[Intrl1 + IntrMaskSet]) = sts;

	for (i = 1; i < nelem(ctlr->rx); i++)
		if (sts & ctlr->rx[i].intmask)
			wakeup(&ctlr->rx[i]);

	for (i = 1; i < nelem(ctlr->tx); i++)
		if (sts & ctlr->tx[i].intmask)
			wakeup(&ctlr->tx[i]);
}

static void
setdma(Desc *d, void *v)
{
	u64int pa = PADDR(v);
	REG(d->d[1]) = pa;
	REG(d->d[2]) = pa >> 32;
}

static void
replenish(Desc *d)
{
	d->b = allocb(Rbsz);
	cachedwbse(d->b->rp, Rbsz);
	setdma(d, d->b->rp);
}

static int
rxdone(void *arg)
{
	Ring *r = arg;

	r->wp = REG(r->regs[RxWP]) & 0xFFFF;
	if (r->rp != r->wp)
		return 1;
	REG(r->intregs[IntrMaskClr]) = r->intmask;
	return 0;
}

static void
recvproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Desc *d;
	Block *b;
	u32int s;

	while (waserror())
		;

	for (;;)
	{
		if (ctlr->rx->rp == ctlr->rx->wp)
		{
			sleep(ctlr->rx, rxdone, ctlr->rx);
			continue;
		}
		d = &ctlr->rx->d[ctlr->rx->rp & ctlr->rx->m];
		b = d->b;
		cachedinvse(b->rp, Rbsz);
		s = REG(d->d[0]);
		replenish(d);
		coherence();
		ctlr->rx->rp = (ctlr->rx->rp + 1) & 0xFFFF;
		REG(ctlr->rx->regs[RxRP]) = ctlr->rx->rp;
		if ((s & (DmaSOP | DmaEOP | DmaRxErrors)) != (DmaSOP | DmaEOP))
		{
			freeb(b);
			continue;
		}
		b->wp += (s & 0x0FFF0000) >> 16;
		etheriq(edev, b, 1);
	}
}

static int
txavail(void *arg)
{
	Ring *r = arg;

	return ((r->wp + 1) & r->m) != (r->cp & r->m);
}

static void
sendproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Desc *d;
	Block *b;

	while (waserror())
		;

	for (;;)
	{
		if (!txavail(ctlr->tx))
		{
			sleep(ctlr->avail, txavail, ctlr->tx);
			continue;
		}
		if ((b = qbread(edev->oq, 100000)) == nil)
			break;
		d = &ctlr->tx->d[ctlr->tx->wp & ctlr->tx->m];
		assert(d->b == nil);
		d->b = b;
		cachedwbse(b->rp, BLEN(b));
		setdma(d, b->rp);
		REG(d->d[0]) = BLEN(b) << 16 | DmaTxQtag | DmaSOP | DmaEOP | DmaTxAppendCrc;
		coherence();
		ctlr->tx->wp = (ctlr->tx->wp + 1) & 0xFFFF;
		REG(ctlr->tx->regs[TxWP]) = ctlr->tx->wp;
	}
}

static int
txdone(void *arg)
{
	Ring *r = arg;

	if (r->cp != r->wp)
	{
		r->rp = REG(r->regs[TxRP]) & 0xFFFF;
		if (r->cp != r->rp)
			return 1;
	}
	REG(r->intregs[IntrMaskClr]) = r->intmask;
	return 0;
}

static void
freeproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Desc *d;

	while (waserror())
		;

	for (;;)
	{
		if (ctlr->tx->cp == ctlr->tx->rp)
		{
			wakeup(ctlr->avail);
			sleep(ctlr->tx, txdone, ctlr->tx);
			continue;
		}
		d = &ctlr->tx->d[ctlr->tx->cp & ctlr->tx->m];
		assert(d->b != nil);
		freeb(d->b);
		d->b = nil;
		coherence();
		ctlr->tx->cp = (ctlr->tx->cp + 1) & 0xFFFF;
	}
}

static void
initring(Ring *ring, Desc *desc, int start, int size)
{
	ring->d = &desc[start];
	ring->m = size - 1;
	ring->cp = ring->rp = ring->wp = 0;
	REG(ring->regs[RxWP]) = 0;
	REG(ring->regs[RxRP]) = 0;
	REG(ring->regs[DmaStart]) = start * 3;
	REG(ring->regs[DmaEnd]) = (start + size) * 3 - 1;
	REG(ring->regs[RdmaWP]) = start * 3;
	REG(ring->regs[RdmaRP]) = start * 3;
	REG(ring->regs[DmaRingBufSize]) = (size << 16) | Rbsz;
	REG(ring->regs[DmaDoneThresh]) = 1;
}

static void
introff(Ctlr *ctlr)
{
	REG(ctlr->regs[Intrl0 + IntrMaskSet]) = -1;
	REG(ctlr->regs[Intrl0 + IntrClr]) = -1;
	REG(ctlr->regs[Intrl1 + IntrMaskSet]) = -1;
	REG(ctlr->regs[Intrl1 + IntrClr]) = -1;
}

static void
dmaoff(Ctlr *ctlr)
{
	REG(ctlr->rx->regs[DmaCtrl]) &= ~(RxRingCfgMask << 1 | DmaCtrlEn);
	REG(ctlr->tx->regs[DmaCtrl]) &= ~(TxRingCfgMask << 1 | DmaCtrlEn);

	REG(ctlr->regs[UmacTxFlush]) = 1;
	microdelay(10);
	REG(ctlr->regs[UmacTxFlush]) = 0;

	while ((REG(ctlr->rx->regs[DmaStatus]) & DmaStatusDis) == 0)
		microdelay(10);
	while ((REG(ctlr->tx->regs[DmaStatus]) & DmaStatusDis) == 0)
		microdelay(10);
}

static void
dmaon(Ctlr *ctlr)
{
	REG(ctlr->rx->regs[DmaCtrl]) |= DmaCtrlEn;
	REG(ctlr->tx->regs[DmaCtrl]) |= DmaCtrlEn;

	while (REG(ctlr->rx->regs[DmaStatus]) & DmaStatusDis)
		microdelay(10);
	while (REG(ctlr->tx->regs[DmaStatus]) & DmaStatusDis)
		microdelay(10);
}

static void
allocbufs(Ctlr *ctlr)
{
	int i;

	if (scratch == nil)
	{
		scratch = allocb(Rbsz);
		memset(scratch->rp, 0xFF, Rbsz);
		cachedwbse(scratch->rp, Rbsz);
	}

	for (i = 0; i < nelem(ctlr->rd); i++)
	{
		ctlr->rd[i].d = &ctlr->regs[RdmaOffset + i * 3];
		replenish(&ctlr->rd[i]);
	}

	for (i = 0; i < nelem(ctlr->td); i++)
	{
		ctlr->td[i].d = &ctlr->regs[TdmaOffset + i * 3];
		setdma(&ctlr->td[i], scratch->rp);
		REG(ctlr->td[i].d[0]) = DmaTxUnderrun;
	}
}

static void
freebufs(Ctlr *ctlr)
{
	int i;

	for (i = 0; i < nelem(ctlr->rd); i++)
	{
		if (ctlr->rd[i].b != nil)
		{
			freeb(ctlr->rd[i].b);
			ctlr->rd[i].b = nil;
		}
	}
	for (i = 0; i < nelem(ctlr->td); i++)
	{
		if (ctlr->td[i].b != nil)
		{
			freeb(ctlr->td[i].b);
			ctlr->td[i].b = nil;
		}
	}
}

static void
initrings(Ctlr *ctlr)
{
	u32int rcfg, tcfg, dmapri[3];
	int i;

	ctlr->rx->intregs = &ctlr->regs[Intrl0];
	ctlr->rx->intmask = IrqRxDmaDone;
	ctlr->rx->num = 16;
	rcfg = 1 << 16;
	for (i = 1; i < nelem(ctlr->rx); i++)
	{
		ctlr->rx[i].regs = &ctlr->regs[RdmaOffset + nelem(ctlr->rd) * 3 + (i - 1) * RingCfg];
		ctlr->rx[i].intregs = &ctlr->regs[Intrl1];
		ctlr->rx[i].intmask = 0x10000 << (i - 1);
		ctlr->rx[i].num = i - 1;
		rcfg |= 1 << (i - 1);
	}
	assert(rcfg && (rcfg & ~RxRingCfgMask) == 0);

	ctlr->tx->intregs = &ctlr->regs[Intrl0];
	ctlr->tx->intmask = IrqTxDmaDone;
	ctlr->tx->num = 16;
	tcfg = 1 << 16;
	for (i = 1; i < nelem(ctlr->tx); i++)
	{
		ctlr->tx[i].regs = &ctlr->regs[TdmaOffset + nelem(ctlr->td) * 3 + (i - 1) * RingCfg];
		ctlr->tx[i].intregs = &ctlr->regs[Intrl1];
		ctlr->tx[i].intmask = 1 << (i - 1);
		ctlr->tx[i].num = i - 1;
		tcfg |= 1 << (i - 1);
	}
	assert(tcfg && (tcfg & ~TxRingCfgMask) == 0);

	REG(ctlr->rx->regs[DmaScbBurstSize]) = 0x08;
	for (i = 1; i < nelem(ctlr->rx); i++)
		initring(&ctlr->rx[i], ctlr->rd, (i - 1) * 32, 32);
	initring(ctlr->rx, ctlr->rd, (i - 1) * 32, nelem(ctlr->rd) - (i - 1) * 32);

	for (i = 0; i < nelem(ctlr->rx); i++)
	{
		REG(ctlr->rx[i].regs[DmaDoneThresh]) = 1;
		REG(ctlr->rx[i].regs[RdmaXonXoffThresh]) = (5 << 16) | ((ctlr->rx[i].m + 1) >> 4);

		// set dma timeout to 50µs
		REG(ctlr->rx->regs[RdmaTimeout0 + ctlr->rx[i].num]) = ((50 * 1000 + 8191) / 8192);
	}

	REG(ctlr->tx->regs[DmaScbBurstSize]) = 0x08;
	for (i = 1; i < nelem(ctlr->tx); i++)
		initring(&ctlr->tx[i], ctlr->td, (i - 1) * 32, 32);
	initring(ctlr->tx, ctlr->td, (i - 1) * 32, nelem(ctlr->td) - (i - 1) * 32);

	dmapri[0] = dmapri[1] = dmapri[2] = 0;
	for (i = 0; i < nelem(ctlr->tx); i++)
	{
		REG(ctlr->tx[i].regs[DmaDoneThresh]) = 10;
		REG(ctlr->tx[i].regs[TdmaFlowPeriod]) = i ? 0 : Maxtu << 16;
		dmapri[ctlr->tx[i].num / 6] |= i << ((ctlr->tx[i].num % 6) * 5);
	}

	REG(ctlr->tx->regs[TdmaArbCtrl]) = 2;
	REG(ctlr->tx->regs[TdmaPriority0]) = dmapri[0];
	REG(ctlr->tx->regs[TdmaPriority1]) = dmapri[1];
	REG(ctlr->tx->regs[TdmaPriority2]) = dmapri[2];

	REG(ctlr->rx->regs[RingCfg]) = rcfg;
	REG(ctlr->tx->regs[RingCfg]) = tcfg;

	REG(ctlr->rx->regs[DmaCtrl]) |= rcfg << 1;
	REG(ctlr->tx->regs[DmaCtrl]) |= tcfg << 1;
}

static void
umaccmd(Ctlr *ctlr, u32int set, u32int clr)
{
	ilock(ctlr);
	REG(ctlr->regs[UmacCmd]) = (REG(ctlr->regs[UmacCmd]) & ~clr) | set;
	iunlock(ctlr);
}

static void
reset(Ctlr *ctlr)
{
	u32int r;

	// reset umac
	r = REG(ctlr->regs[SysRbufFlushCtrl]);
	REG(ctlr->regs[SysRbufFlushCtrl]) = r | 2;
	microdelay(10);
	REG(ctlr->regs[SysRbufFlushCtrl]) = r & ~2;
	microdelay(10);

	// umac reset
	REG(ctlr->regs[SysRbufFlushCtrl]) = 0;
	microdelay(10);

	REG(ctlr->regs[UmacCmd]) = 0;
	REG(ctlr->regs[UmacCmd]) = CmdSwReset | CmdLclLoopEn;
	microdelay(2);
	REG(ctlr->regs[UmacCmd]) = 0;
}

static void
setmac(Ctlr *ctlr, uchar *ea)
{
	REG(ctlr->regs[UmacMac0]) = ea[0] << 24 | ea[1] << 16 | ea[2] << 8 | ea[3];
	REG(ctlr->regs[UmacMac1]) = ea[4] << 8 | ea[5];
}

static void
sethfb(Ctlr *ctlr)
{
	int i;

	REG(ctlr->regs[HfbCtlr]) = 0;
	REG(ctlr->regs[HfbFltEnable]) = 0;
	REG(ctlr->regs[HfbFltEnable + 1]) = 0;

	for (i = 0; i < 8; i++)
		REG(ctlr->rx->regs[RdmaIndex2Ring0 + i]) = 0;

	for (i = 0; i < 48 / 4; i++)
		REG(ctlr->regs[HfbFltLen + i]) = 0;

	for (i = 0; i < 48 * 128; i++)
		REG(ctlr->regs[HfbOffset + i]) = 0;
}

static int
mdiodone(void *arg)
{
	Ctlr *ctlr = arg;
	REG(ctlr->regs[Intrl0 + IntrMaskClr]) = (IrqMdioDone | IrqMdioError);
	return (REG(ctlr->regs[MdioCmd]) & MdioStartBusy) == 0;
}

static int
mdiowait(Ctlr *ctlr)
{
	REG(ctlr->regs[MdioCmd]) |= MdioStartBusy;
	while (REG(ctlr->regs[MdioCmd]) & MdioStartBusy)
		tsleep(ctlr->mii, mdiodone, ctlr, 10);
	return 0;
}

static int
mdiow(Mii* mii, int phy, int addr, int data)
{
	Ctlr *ctlr = mii->ctlr;

	if (phy > MdioPhyMask)
		return -1;
	addr &= MdioAddrMask;
	REG(ctlr->regs[MdioCmd]) = MdioWrite | (phy << MdioPhyShift) | (addr << MdioAddrShift) | (data & 0xFFFF);
	return mdiowait(ctlr);
}

static int
mdior(Mii* mii, int phy, int addr)
{
	Ctlr *ctlr = mii->ctlr;

	if (phy > MdioPhyMask)
		return -1;
	addr &= MdioAddrMask;
	REG(ctlr->regs[MdioCmd]) = MdioRead | (phy << MdioPhyShift) | (addr << MdioAddrShift);
	if (mdiowait(ctlr) < 0)
		return -1;
	if (REG(ctlr->regs[MdioCmd]) & MdioReadFail)
		return -1;
	return REG(ctlr->regs[MdioCmd]) & 0xFFFF;
}

static int
bcmshdr(Mii *mii, int reg)
{
	miimiw(mii, 0x1C, (reg & 0x1F) << 10);
	return miimir(mii, 0x1C) & 0x3FF;
}

static int
bcmshdw(Mii *mii, int reg, int dat)
{
	return miimiw(mii, 0x1C, 0x8000 | (reg & 0x1F) << 10 | (dat & 0x3FF));
}

static int
linkevent(void *arg)
{
	Ctlr *ctlr = arg;
	REG(ctlr->regs[Intrl0 + IntrMaskClr]) = IrqLinkUp | IrqLinkDown;
	return 0;
}

static void
linkproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	MiiPhy *phy;
	int link = -1;

	while (waserror())
		;

	for (;;)
	{
		tsleep(ctlr->link, linkevent, ctlr, 1000);
		miistatus(ctlr->mii);
		phy = ctlr->mii->curphy;
		if (phy == nil || phy->link == link)
			continue;
		link = phy->link;
		if (link)
		{
			u32int cmd = CmdRxEn | CmdTxEn;
			switch (phy->speed)
			{
			case 1000:
				cmd |= CmdSpeed1000;
				break;
			case 100:
				cmd |= CmdSpeed100;
				break;
			case 10:
				cmd |= CmdSpeed10;
				break;
			}
			if (!phy->fd)
				cmd |= CmdHdEn;
			if (!phy->rfc)
				cmd |= CmdRxPauseIgn;
			if (!phy->tfc)
				cmd |= CmdTxPauseIgn;

			REG(ctlr->regs[ExtRgmiiOobCtrl]) = (REG(ctlr->regs[ExtRgmiiOobCtrl]) & ~OobDisable) | RgmiiLink;
			umaccmd(ctlr, cmd, CmdSpeedMask | CmdHdEn | CmdRxPauseIgn | CmdTxPauseIgn);

			edev->mbps = phy->speed;
		}
		edev->link = link;
		// print("#l%d: link %d speed %d\n", edev->ctlrno, edev->link, edev->mbps);
	}
}

static void
setmdfaddr(Ctlr *ctlr, int i, uchar *ea)
{
	REG(ctlr->regs[UmacMdfAddr0 + i * 2 + 0]) = ea[0] << 8 | ea[1];
	REG(ctlr->regs[UmacMdfAddr0 + i * 2 + 1]) = ea[2] << 24 | ea[3] << 16 | ea[4] << 8 | ea[5];
}

static void
rxmode(Ether *edev, int prom)
{
	Ctlr *ctlr = edev->ctlr;
	Netaddr *na;
	int i;

	if (prom || edev->nmaddr > 16 - 2)
	{
		REG(ctlr->regs[UmacMdfCtrl]) = 0;
		umaccmd(ctlr, CmdProm, 0);
		return;
	}
	setmdfaddr(ctlr, 0, edev->bcast);
	setmdfaddr(ctlr, 1, edev->ea);
	for (i = 2, na = edev->maddr; na != nil; na = na->next, i++)
		setmdfaddr(ctlr, i, na->addr);
	REG(ctlr->regs[UmacMdfCtrl]) = (-0x10000 >> i) & 0x1FFFF;
	umaccmd(ctlr, 0, CmdProm);
}

static void
shutdown(Ether *edev)
{
	Ctlr *ctlr = edev->ctlr;

	dmaoff(ctlr);
	introff(ctlr);
}

static void
attach(Ether *edev)
{
	Ctlr *ctlr = edev->ctlr;

	qlock(ctlr);
	if (ctlr->attached)
	{
		qunlock(ctlr);
		return;
	}
	if (waserror())
	{
		print("#l%d: %s\n", edev->ctlrno, up->errstr);
		shutdown(edev);
		freebufs(ctlr);
		qunlock(ctlr);
		nexterror();
	}

	// statistics
	REG(ctlr->regs[UmacMibCtrl]) = MibResetRx | MibResetTx | MibResetRunt;
	REG(ctlr->regs[UmacMibCtrl]) = 0;

	// wol
	REG(ctlr->regs[UmacMpdCtrl]) &= ~(MpdPwEn | MpdEn);

	// power
	REG(ctlr->regs[UmacEeeCtrl]) &= ~UmacEeeEn;
	REG(ctlr->regs[RbufEnergyCtrl]) &= ~(RbufEeeEn | RbufPmEn);
	REG(ctlr->regs[TbufEnergyCtrl]) &= ~(RbufEeeEn | RbufPmEn);
	REG(ctlr->regs[TbufBpMc]) = 0;

	REG(ctlr->regs[UmacMaxFrameLen]) = Maxtu;

	REG(ctlr->regs[RbufTbufSizeCtrl]) = 1;

	REG(ctlr->regs[TbufCtrl]) &= ~(Rbuf64En);
	REG(ctlr->regs[RbufCtrl]) &= ~(Rbuf64En | RbufAlign2B);
	REG(ctlr->regs[RbufChkCtrl]) &= ~(RbufChkRxChkEn | RbufChkSkipFcs);

	allocbufs(ctlr);
	initrings(ctlr);
	dmaon(ctlr);

	setmac(ctlr, edev->ea);
	sethfb(ctlr);
	rxmode(edev, 0);

	REG(ctlr->regs[SysPortCtrl]) = PortModeExtGphy;
	REG(ctlr->regs[ExtRgmiiOobCtrl]) |= RgmiiModeEn | IdModeDis;

	ctlr->mii->ctlr = ctlr;
	ctlr->mii->mir = mdior;
	ctlr->mii->miw = mdiow;
	mii(ctlr->mii, ~0);

	if (ctlr->mii->curphy == nil)
		error("no phy");

	print("#l%d: phy%d id %.8ux oui %x\n",
		  edev->ctlrno, ctlr->mii->curphy->phyno,
		  ctlr->mii->curphy->id, ctlr->mii->curphy->oui);

	miireset(ctlr->mii);

	switch (ctlr->mii->curphy->id)
	{
	case 0x600d84a2: /* BCM54312PE */
		/* mask interrupts */
		miimiw(ctlr->mii, 0x10, miimir(ctlr->mii, 0x10) | 0x1000);

		/* SCR3: clear DLLAPD_DIS */
		bcmshdw(ctlr->mii, 0x05, bcmshdr(ctlr->mii, 0x05) & ~0x0002);
		/* APD: set APD_EN */
		bcmshdw(ctlr->mii, 0x0a, bcmshdr(ctlr->mii, 0x0a) | 0x0020);

		/* blinkenlights */
		bcmshdw(ctlr->mii, 0x09, bcmshdr(ctlr->mii, 0x09) | 0x0010);
		bcmshdw(ctlr->mii, 0x0d, 3 << 0 | 0 << 4);
		break;
	}

	/* don't advertise EEE */
	miimmdw(ctlr->mii, 7, 60, 0);

	miiane(ctlr->mii, ~0, AnaAP | AnaP, ~0);

	ctlr->attached = 1;

	kproc("genet-recv", recvproc, edev);
	kproc("genet-send", sendproc, edev);
	kproc("genet-free", freeproc, edev);
	kproc("genet-link", linkproc, edev);

	qunlock(ctlr);
	poperror();
}

static void
prom(void *arg, int on)
{
	Ether *edev = arg;
	rxmode(edev, on);
}

static void
multi(void *arg, uchar*, int)
{
	Ether *edev = arg;
	rxmode(edev, edev->prom > 0);
}

static int
pnp(Ether *edev)
{
	static Ctlr ctlr[1];

	if (ctlr->regs != nil)
		return -1;

	ctlr->regs = (u32int *)(VIRTIO + 0x580000);
	ctlr->rx->regs = &ctlr->regs[RdmaOffset + nelem(ctlr->rd) * 3 + 16 * RingCfg];
	ctlr->tx->regs = &ctlr->regs[TdmaOffset + nelem(ctlr->td) * 3 + 16 * RingCfg];

	edev->port = (uintptr)ctlr->regs;
	edev->irq = IRQether;
	edev->ctlr = ctlr;
	edev->attach = attach;
	edev->shutdown = shutdown;
	edev->promiscuous = prom;
	edev->multicast = multi;
	edev->arg = edev;
	edev->mbps = 1000;
	edev->maxmtu = Maxtu;

	parseether(edev->ea, getethermac());

	reset(ctlr);
	dmaoff(ctlr);
	introff(ctlr);

	intrenable(edev->irq + 0, interrupt0, edev, BUSUNKNOWN, edev->name);
	intrenable(edev->irq + 1, interrupt1, edev, BUSUNKNOWN, edev->name);

	return 0;
}

void
ethergenetlink(void)
{
	addethercard("genet", pnp);
}
achedwbse(scratch->rp, Rbsz);
}

for (i = 0; i < nelem(ctlr->rd); i++)
{
	ctlr->rd[i].d = &ctlr->regs[RdmaOffset + i * 3];
	replenish(&ctlr->rd[i]);
}

	for(i = 0; i < nelem(etherif.h                                                                                              664       0       0           35 12044177162  10753                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../omap/etherif.h"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   ethermii.c                                                                                             644       0       0        12077 13530716010  11205                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "etherif.h"

#include "ethermii.h"

int
mii(Mii* mii, int mask)
{
	MiiPhy *miiphy;
	int bit, oui, phyno, rmask;
	u32int id;

	/*
	 * Probe through mii for PHYs in mask;
	 * return the mask of those found in the current probe.
	 * If the PHY has not already been probed, update
	 * the Mii information.
	 */
	rmask = 0;
	for (phyno = 0; phyno < NMiiPhy; phyno++)
	{
		bit = 1 << phyno;
		if (!(mask & bit))
			continue;
		if (mii->mask & bit)
		{
			rmask |= bit;
			continue;
		}
		if (mii->mir(mii, phyno, Bmsr) == -1)
			continue;
		id = mii->mir(mii, phyno, Phyidr1) << 16;
		id |= mii->mir(mii, phyno, Phyidr2);
		oui = (id & 0x3FFFFC00) >> 10;
		if (oui == 0xFFFFF || oui == 0)
			continue;

		if ((miiphy = malloc(sizeof(MiiPhy))) == nil)
			continue;

		miiphy->mii = mii;
		miiphy->id = id;
		miiphy->oui = oui;
		miiphy->phyno = phyno;

		miiphy->anar = ~0;
		miiphy->fc = ~0;
		miiphy->mscr = ~0;

		mii->phy[phyno] = miiphy;
		if (mii->curphy == nil)
			mii->curphy = miiphy;
		mii->mask |= bit;
		mii->nphy++;

		rmask |= bit;
	}
	return rmask;
}

int
miimir(Mii* mii, int r)
{
	if (mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	return mii->mir(mii, mii->curphy->phyno, r);
}

int
miimiw(Mii* mii, int r, int data)
{
	if (mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	return mii->miw(mii, mii->curphy->phyno, r, data);
}

int
miireset(Mii* mii)
{
	int bmcr;

	if (mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	bmcr = mii->mir(mii, mii->curphy->phyno, Bmcr);
	bmcr |= BmcrR;
	mii->miw(mii, mii->curphy->phyno, Bmcr, bmcr);
	microdelay(1);

	return 0;
}

int
miiane(Mii* mii, int a, int p, int e)
{
	int anar, bmsr, mscr, r, phyno;

	if (mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	phyno = mii->curphy->phyno;

	bmsr = mii->mir(mii, phyno, Bmsr);
	if (!(bmsr & BmsrAna))
		return -1;

	if (a != ~0)
		anar = (AnaTXFD | AnaTXHD | Ana10FD | Ana10HD) & a;
	else if (mii->curphy->anar != ~0)
		anar = mii->curphy->anar;
	else
	{
		anar = mii->mir(mii, phyno, Anar);
		anar &= ~(AnaAP | AnaP | AnaT4 | AnaTXFD | AnaTXHD | Ana10FD | Ana10HD);
		if (bmsr & Bmsr10THD)
			anar |= Ana10HD;
		if (bmsr & Bmsr10TFD)
			anar |= Ana10FD;
		if (bmsr & Bmsr100TXHD)
			anar |= AnaTXHD;
		if (bmsr & Bmsr100TXFD)
			anar |= AnaTXFD;
	}
	mii->curphy->anar = anar;

	if (p != ~0)
		anar |= (AnaAP | AnaP) & p;
	else if (mii->curphy->fc != ~0)
		anar |= mii->curphy->fc;
	mii->curphy->fc = (AnaAP | AnaP) & anar;

	if (bmsr & BmsrEs)
	{
		mscr = mii->mir(mii, phyno, Mscr);
		mscr &= ~(Mscr1000TFD | Mscr1000THD);
		if (e != ~0)
			mscr |= (Mscr1000TFD | Mscr1000THD) & e;
		else if (mii->curphy->mscr != ~0)
			mscr = mii->curphy->mscr;
		else
		{
			r = mii->mir(mii, phyno, Esr);
			if (r & Esr1000THD)
				mscr |= Mscr1000THD;
			if (r & Esr1000TFD)
				mscr |= Mscr1000TFD;
		}
		mii->curphy->mscr = mscr;
		mii->miw(mii, phyno, Mscr, mscr);
	}
	mii->miw(mii, phyno, Anar, anar);

	r = mii->mir(mii, phyno, Bmcr);
	if (!(r & BmcrR))
	{
		r |= BmcrAne | BmcrRan;
		mii->miw(mii, phyno, Bmcr, r);
	}

	return 0;
}

int
miistatus(Mii* mii)
{
	MiiPhy *phy;
	int anlpar, bmsr, p, r, phyno;

	if (mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	phy = mii->curphy;
	phyno = phy->phyno;

	/*
	 * Check Auto-Negotiation is complete and link is up.
	 * (Read status twice as the Ls bit is sticky).
	 */
	bmsr = mii->mir(mii, phyno, Bmsr);
	if (!(bmsr & (BmsrAnc | BmsrAna)))
	{
		// print("miistatus: auto-neg incomplete\n");
		return -1;
	}

	bmsr = mii->mir(mii, phyno, Bmsr);
	if (!(bmsr & BmsrLs))
	{
		// print("miistatus: link down\n");
		phy->link = 0;
		return -1;
	}

	phy->speed = phy->fd = phy->rfc = phy->tfc = 0;
	if (phy->mscr)
	{
		r = mii->mir(mii, phyno, Mssr);
		if ((phy->mscr & Mscr1000TFD) && (r & Mssr1000TFD))
		{
			phy->speed = 1000;
			phy->fd = 1;
		}
		else if ((phy->mscr & Mscr1000THD) && (r & Mssr1000THD))
			phy->speed = 1000;
	}

	anlpar = mii->mir(mii, phyno, Anlpar);
	if (phy->speed == 0)
	{
		r = phy->anar & anlpar;
		if (r & AnaTXFD)
		{
			phy->speed = 100;
			phy->fd = 1;
		}
		else if (r & AnaTXHD)
			phy->speed = 100;
		else if (r & Ana10FD)
		{
			phy->speed = 10;
			phy->fd = 1;
		}
		else if (r & Ana10HD)
			phy->speed = 10;
	}
	if (phy->speed == 0)
	{
		// print("miistatus: phy speed 0\n");
		return -1;
	}

	if (phy->fd)
	{
		p = phy->fc;
		r = anlpar & (AnaAP | AnaP);
		if (p == AnaAP && r == (AnaAP | AnaP))
			phy->tfc = 1;
		else if (p == (AnaAP | AnaP) && r == AnaAP)
			phy->rfc = 1;
		else if ((p & AnaP) && (r & AnaP))
			phy->rfc = phy->tfc = 1;
	}

	phy->link = 1;

	return 0;
}

int
miimmdr(Mii* mii, int a, int r)
{
	a &= 0x1F;
	if (miimiw(mii, Mmdctrl, a) == -1)
		return -1;
	if (miimiw(mii, Mmddata, r) == -1)
		return -1;
	if (miimiw(mii, Mmdctrl, a | 0x4000) == -1)
		return -1;
	return miimir(mii, Mmddata);
}

int
miimmdw(Mii* mii, int a, int r, int data)
{
	a &= 0x1F;
	if (miimiw(mii, Mmdctrl, a) == -1)
		return -1;
	if (miimiw(mii, Mmddata, r) == -1)
		return -1;
	if (miimiw(mii, Mmdctrl, a | 0x4000) == -1)
		return -1;
	return miimiw(mii, Mmddata, data);
}
	umaccmd(ctlr, 0, CmdProm);
	}

	static void
	shutdown(Ether *edev)
	{
		Ctlr *ctlr = edev->ctlr;

		dmaoff(ctlr);
		introff(ctlr);
	}

	static void
	attach(Ether *edev)
	{
		Ctlr *ctlr = edev->ctlr;

		qlock(ctlr);
		if (ctlr->attached)
		{
			qunlock(ctlr);
			return;
		}
		if (waserror())
		{
			print("#l%d: %s\n", edev->ctlrno, up->errstr);
			shutdown(edev);
			freebufs(ctlr);
			qunlock(ctlr);
			nexterror();
		}

		// statistics
		REG(ctlr->regs[UmacMibCtrl]) = MibResetRxethermii.h                                                                                             644       0       0         6562 13530716010 11174                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    typedef struct Mii Mii;
		typedef struct MiiPhy MiiPhy;

		enum
		{				  /* registers */
		  Bmcr = 0x00,	/* Basic Mode Control */
		  Bmsr = 0x01,	/* Basic Mode Status */
		  Phyidr1 = 0x02, /* PHY Identifier #1 */
		  Phyidr2 = 0x03, /* PHY Identifier #2 */
		  Anar = 0x04,	/* Auto-Negotiation Advertisement */
		  Anlpar = 0x05,  /* AN Link Partner Ability */
		  Aner = 0x06,	/* AN Expansion */
		  Annptr = 0x07,  /* AN Next Page TX */
		  Annprr = 0x08,  /* AN Next Page RX */
		  Mscr = 0x09,	/* MASTER-SLAVE Control */
		  Mssr = 0x0A,	/* MASTER-SLAVE Status */
		  Mmdctrl = 0x0D, /* MMD Access Control */
		  Mmddata = 0x0E, /* MMD Access Data Register */
		  Esr = 0x0F,	 /* Extended Status */

		  NMiiPhyr = 32,
		  NMiiPhy = 32,
		};

		enum
		{					/* Bmcr */
		  BmcrSs1 = 0x0040, /* Speed Select[1] */
		  BmcrCte = 0x0080, /* Collision Test Enable */
		  BmcrDm = 0x0100,  /* Duplex Mode */
		  BmcrRan = 0x0200, /* Restart Auto-Negotiation */
		  BmcrI = 0x0400,   /* Isolate */
		  BmcrPd = 0x0800,  /* Power Down */
		  BmcrAne = 0x1000, /* Auto-Negotiation Enable */
		  BmcrSs0 = 0x2000, /* Speed Select[0] */
		  BmcrLe = 0x4000,  /* Loopback Enable */
		  BmcrR = 0x8000,   /* Reset */
		};

		enum
		{						/* Bmsr */
		  BmsrEc = 0x0001,		/* Extended Capability */
		  BmsrJd = 0x0002,		/* Jabber Detect */
		  BmsrLs = 0x0004,		/* Link Status */
		  BmsrAna = 0x0008,		/* Auto-Negotiation Ability */
		  BmsrRf = 0x0010,		/* Remote Fault */
		  BmsrAnc = 0x0020,		/* Auto-Negotiation Complete */
		  BmsrPs = 0x0040,		/* Preamble Suppression Capable */
		  BmsrEs = 0x0100,		/* Extended Status */
		  Bmsr100T2HD = 0x0200, /* 100BASE-T2 HD Capable */
		  Bmsr100T2FD = 0x0400, /* 100BASE-T2 FD Capable */
		  Bmsr10THD = 0x0800,   /* 10BASE-T HD Capable */
		  Bmsr10TFD = 0x1000,   /* 10BASE-T FD Capable */
		  Bmsr100TXHD = 0x2000, /* 100BASE-TX HD Capable */
		  Bmsr100TXFD = 0x4000, /* 100BASE-TX FD Capable */
		  Bmsr100T4 = 0x8000,   /* 100BASE-T4 Capable */
		};

		enum
		{					/* Anar/Anlpar */
		  Ana10HD = 0x0020, /* Advertise 10BASE-T */
		  Ana10FD = 0x0040, /* Advertise 10BASE-T FD */
		  AnaTXHD = 0x0080, /* Advertise 100BASE-TX */
		  AnaTXFD = 0x0100, /* Advertise 100BASE-TX FD */
		  AnaT4 = 0x0200,   /* Advertise 100BASE-T4 */
		  AnaP = 0x0400,	/* Pause */
		  AnaAP = 0x0800,   /* Asymmetrical Pause */
		  AnaRf = 0x2000,   /* Remote Fault */
		  AnaAck = 0x4000,  /* Acknowledge */
		  AnaNp = 0x8000,   /* Next Page Indication */
		};

		enum
		{						/* Mscr */
		  Mscr1000THD = 0x0100, /* Advertise 1000BASE-T HD */
		  Mscr1000TFD = 0x0200, /* Advertise 1000BASE-T FD */
		};

		enum
		{						/* Mssr */
		  Mssr1000THD = 0x0400, /* Link Partner 1000BASE-T HD able */
		  Mssr1000TFD = 0x0800, /* Link Partner 1000BASE-T FD able */
		};

		enum
		{					   /* Esr */
		  Esr1000THD = 0x1000, /* 1000BASE-T HD Capable */
		  Esr1000TFD = 0x2000, /* 1000BASE-T FD Capable */
		  Esr1000XHD = 0x4000, /* 1000BASE-X HD Capable */
		  Esr1000XFD = 0x8000, /* 1000BASE-X FD Capable */
		};

		typedef struct Mii
		{
			Lock;
			int nphy;
			int mask;
			MiiPhy *phy[NMiiPhy];
			MiiPhy *curphy;

			void *ctlr;
			int (*mir)(Mii *, int, int);
			int (*miw)(Mii *, int, int, int);
		} Mii;

		typedef struct MiiPhy
		{
			Mii *mii;
			u32int id;
			int oui;
			int phyno;

			int anar;
			int fc;
			int mscr;

			int link;
			int speed;
			int fd;
			int rfc;
			int tfc;
		};

		extern int mii(Mii *, int);
		extern int miiane(Mii *, int, int, int);
		extern int miimir(Mii *, int);
		extern int miimiw(Mii *, int, int);
		extern int miireset(Mii *);
		extern int miistatus(Mii *);

		extern int miimmdr(Mii *, int, int);
		extern int miimmdw(Mii *, int, int, int);
		                                                                                                                                              etherusb.c                                                                                             664       0       0        21270 13514062573 11227                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Kernel proxy for usb ethernet device
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"
#include "../ip/ip.h"

#define GET4(p) ((p)[3] << 24 | (p)[2] << 16 | (p)[1] << 8 | (p)[0])
#define PUT4(p, v) ((p)[0] = (v), (p)[1] = (v) >> 8, \
					(p)[2] = (v) >> 16, (p)[3] = (v) >> 24)
#define dprint \
	if (debug) \
	print
#define ddump \
	if (0)    \
	dump

			static int debug = 0;

		enum
		{
			Bind = 0,
			Unbind,

			SmscRxerror = 0x8000,
			SmscTxfirst = 0x2000,
			SmscTxlast = 0x1000,
			Lan78Rxerror = 0x00400000,
			Lan78Txfcs = 1 << 22,
		};

		typedef struct Ctlr Ctlr;
		typedef struct Udev Udev;

		typedef int(Unpackfn)(Ether *, Block *);
		typedef void(Transmitfn)(Ctlr *, Block *);

		struct Ctlr
		{
			Ether *edev;
			Udev *udev;
			Chan *inchan;
			Chan *outchan;
			char *buf;
			int bufsize;
			int maxpkt;
			uint rxbuf;
			uint rxpkt;
			uint txbuf;
			uint txpkt;
			QLock;
		};

		struct Udev
		{
			char *name;
			Unpackfn *unpack;
			Transmitfn *transmit;
		};

		static Cmdtab cmds[] = {
			{
				Bind,
				"bind",
				7,
			},
			{
				Unbind,
				"unbind",
				0,
			},
		};

		static Unpackfn unpackcdc, unpackasix, unpacksmsc, unpacklan78;
		static Transmitfn transmitcdc, transmitasix, transmitsmsc, transmitlan78;

		static Udev udevtab[] = {
			{
				"cdc",
				unpackcdc,
				transmitcdc,
			},
			{
				"asix",
				unpackasix,
				transmitasix,
			},
			{
				"smsc",
				unpacksmsc,
				transmitsmsc,
			},
			{
				"lan78xx",
				unpacklan78,
				transmitlan78,
			},
			{nil},
		};

		static char nullea[Eaddrlen];

		static void
		dump(int c, Block *b)
		{
			int s, i;

			s = splhi();
			print("%c%ld:", c, BLEN(b));
			for (i = 0; i < 32; i++)
				print(" %2.2ux", b->rp[i]);
			print("\n");
			splx(s);
		}

		static int
		unpack(Ether * edev, Block * b, int m)
		{
			Block *nb;
			Ctlr *ctlr;

			ctlr = edev->ctlr;
			ddump('?', b);
			if (m == BLEN(b))
			{
				etheriq(edev, b, 1);
				ctlr->rxpkt++;
				return 1;
			}
			nb = iallocb(m);
			if (nb != nil)
			{
				memmove(nb->wp, b->rp, m);
				nb->wp += m;
				etheriq(edev, nb, 1);
				ctlr->rxpkt++;
			}
			else
				edev->soverflows++;
			b->rp += m;
			return 0;
		}

		static int
			unpackcdc(Ether * edev, Block * b)
		{
			int m;

			m = BLEN(b);
			if (m < 6)
				return -1;
			return unpack(edev, b, m);
		}

		static int
			unpackasix(Ether * edev, Block * b)
		{
			ulong hd;
			int m;
			uchar *wp;

			if (BLEN(b) < 4)
				return -1;
			hd = GET4(b->rp);
			b->rp += 4;
			m = hd & 0xFFFF;
			hd >>= 16;
			if (m != (~hd & 0xFFFF))
				return -1;
			m = ROUND(m, 2);
			if (m < 6 || m > BLEN(b))
				return -1;
			if ((wp = b->rp + m) != b->wp && b->wp - wp < 4)
				b->wp = wp;
			return unpack(edev, b, m);
		}

		static int
			unpacksmsc(Ether * edev, Block * b)
		{
			ulong hd;
			int m;

			ddump('@', b);
			if (BLEN(b) < 4)
				return -1;
			hd = GET4(b->rp);
			b->rp += 4;
			m = hd >> 16;
			if (m < 6 || m > BLEN(b))
				return -1;
			if (BLEN(b) - m < 4)
				b->wp = b->rp + m;
			if (hd & SmscRxerror)
			{
				edev->frames++;
				b->rp += m;
				if (BLEN(b) == 0)
				{
					freeb(b);
					return 1;
				}
			}
			else if (unpack(edev, b, m) == 1)
				return 1;
			if ((m &= 3) != 0)
				b->rp += 4 - m;
			return 0;
		}

		static int
			unpacklan78(Ether * edev, Block * b)
		{
			ulong hd;
			int m;

			if (BLEN(b) < 10)
				return -1;
			hd = GET4(b->rp);
			b->rp += 10;
			m = hd & 0x3FFF;
			if (m < 6 || m > BLEN(b))
				return -1;
			if (hd & Lan78Rxerror)
			{
				edev->frames++;
				b->rp += m;
				if (BLEN(b) == 0)
				{
					freeb(b);
					return 1;
				}
			}
			else if (unpack(edev, b, m) == 1)
				return 1;
			if (BLEN(b) > 0)
				b->rp = (uchar *)((((uintptr)b->rp) + 3) & ~3);
			return 0;
		}

		static void
			transmit(Ctlr * ctlr, Block * b)
		{
			Chan *c;

			ddump('!', b);
			c = ctlr->outchan;
			devtab[c->type]->bwrite(c, b, 0);
		}

		static void
			transmitcdc(Ctlr * ctlr, Block * b)
		{
			transmit(ctlr, b);
		}

		static void
			transmitasix(Ctlr * ctlr, Block * b)
		{
			int n;

			n = BLEN(b) & 0xFFFF;
			n |= ~n << 16;
			b = padblock(b, 4);
			PUT4(b->rp, n);
			if (BLEN(b) % ctlr->maxpkt == 0)
			{
				b = padblock(b, -4);
				PUT4(b->wp, 0xFFFF0000);
				b->wp += 4;
			}
			transmit(ctlr, b);
		}

		static void
			transmitsmsc(Ctlr * ctlr, Block * b)
		{
			int n;

			n = BLEN(b) & 0x7FF;
			b = padblock(b, 8);
			PUT4(b->rp, n | SmscTxfirst | SmscTxlast);
			PUT4(b->rp + 4, n);
			transmit(ctlr, b);
		}

		static void
			transmitlan78(Ctlr * ctlr, Block * b)
		{
			int n;

			n = BLEN(b) & 0xFFFFF;
			b = padblock(b, 8);
			PUT4(b->rp, n | Lan78Txfcs);
			PUT4(b->rp + 4, n);
			transmit(ctlr, b);
		}

		static void
		etherusbproc(void *a)
		{
			Ether *edev;
			Ctlr *ctlr;
			Chan *c;
			Block *b;

			edev = a;
			ctlr = edev->ctlr;
			c = ctlr->inchan;
			b = nil;
			if (waserror())
			{
				print("etherusbproc: error exit %s\n", up->errstr);
				pexit(up->errstr, 1);
				return;
			}
			for (;;)
			{
				if (b == nil)
				{
					b = devtab[c->type]->bread(c, ctlr->bufsize, 0);
					ctlr->rxbuf++;
				}
				switch (ctlr->udev->unpack(edev, b))
				{
				case -1:
					edev->buffs++;
					freeb(b);
					/* fall through */
				case 1:
					b = nil;
					break;
				}
			}
		}

		/*
 * bind type indev outdev mac bufsize maxpkt
 */
		static void
			bind(Ctlr * ctlr, Udev * udev, Cmdbuf * cb)
		{
			Chan *inchan, *outchan;
			char *buf;
			uint bufsize, maxpkt;
			uchar ea[Eaddrlen];

			qlock(ctlr);
			inchan = outchan = nil;
			buf = nil;
			if (waserror())
			{
				free(buf);
				if (inchan)
					cclose(inchan);
				if (outchan)
					cclose(outchan);
				qunlock(ctlr);
				nexterror();
			}
			if (ctlr->buf != nil)
				cmderror(cb, "already bound to a device");
			maxpkt = strtol(cb->f[6], 0, 0);
			if (maxpkt < 8 || maxpkt > 512)
				cmderror(cb, "bad maxpkt");
			bufsize = strtol(cb->f[5], 0, 0);
			if (bufsize < maxpkt || bufsize > 32 * 1024)
				cmderror(cb, "bad bufsize");
			buf = smalloc(bufsize);
			inchan = namec(cb->f[2], Aopen, OREAD, 0);
			outchan = namec(cb->f[3], Aopen, OWRITE, 0);
			assert(inchan != nil && outchan != nil);
			if (parsemac(ea, cb->f[4], Eaddrlen) != Eaddrlen)
				cmderror(cb, "bad etheraddr");
			if (memcmp(ctlr->edev->ea, nullea, Eaddrlen) == 0)
				memmove(ctlr->edev->ea, ea, Eaddrlen);
			else if (memcmp(ctlr->edev->ea, ea, Eaddrlen) != 0)
				cmderror(cb, "wrong ether address");
			ctlr->buf = buf;
			ctlr->inchan = inchan;
			ctlr->outchan = outchan;
			ctlr->bufsize = bufsize;
			ctlr->maxpkt = maxpkt;
			ctlr->udev = udev;
			kproc("etherusb", etherusbproc, ctlr->edev);
			memmove(ctlr->edev->addr, ea, Eaddrlen);
			print("\netherusb %s: %E\n", udev->name, ctlr->edev->addr);
			poperror();
			qunlock(ctlr);
		}

		static void
			unbind(Ctlr * ctlr)
		{
			qlock(ctlr);
			if (ctlr->buf != nil)
			{
				free(ctlr->buf);
				ctlr->buf = nil;
				if (ctlr->inchan)
					cclose(ctlr->inchan);
				if (ctlr->outchan)
					cclose(ctlr->outchan);
				ctlr->inchan = ctlr->outchan = nil;
			}
			qunlock(ctlr);
		}

		static long
		etherusbifstat(Ether * edev, void *a, long n, ulong offset)
		{
			Ctlr *ctlr;
			char *p;
			int l;

			ctlr = edev->ctlr;
			p = malloc(READSTR);
			l = 0;

			l += snprint(p + l, READSTR - l, "rxbuf: %ud\n", ctlr->rxbuf);
			l += snprint(p + l, READSTR - l, "rxpkt: %ud\n", ctlr->rxpkt);
			l += snprint(p + l, READSTR - l, "txbuf: %ud\n", ctlr->txbuf);
			l += snprint(p + l, READSTR - l, "txpkt: %ud\n", ctlr->txpkt);
			USED(l);

			n = readstr(offset, a, n, p);
			free(p);
			return n;
		}

		static void
			etherusbtransmit(Ether * edev)
		{
			Ctlr *ctlr;
			Block *b;

			ctlr = edev->ctlr;
			while ((b = qget(edev->oq)) != nil)
			{
				ctlr->txpkt++;
				if (ctlr->buf == nil)
					freeb(b);
				else
				{
					ctlr->udev->transmit(ctlr, b);
					ctlr->txbuf++;
				}
			}
		}

		static long
		etherusbctl(Ether * edev, void *buf, long n)
		{
			Ctlr *ctlr;
			Cmdbuf *cb;
			Cmdtab *ct;
			Udev *udev;

			if ((ctlr = edev->ctlr) == nil)
				error(Enonexist);

			cb = parsecmd(buf, n);
			if (waserror())
			{
				free(cb);
				nexterror();
			}
			ct = lookupcmd(cb, cmds, nelem(cmds));
			switch (ct->index)
			{
			case Bind:
				for (udev = udevtab; udev->name; udev++)
					if (strcmp(cb->f[1], udev->name) == 0)
						break;
				if (udev->name == nil)
					cmderror(cb, "unknown etherusb type");
				bind(ctlr, udev, cb);
				break;
			case Unbind:
				unbind(ctlr);
				break;
			default:
				cmderror(cb, "unknown etherusb control message");
			}
			poperror();
			free(cb);
			return n;
		}

		static void
		etherusbmulticast(void *, uchar *, int)
		{
			/* nothing to do, we allow all multicast packets in */
		}

		static void
		etherusbshutdown(Ether *)
		{
		}

		static void
			etherusbattach(Ether * edev)
		{
			Ctlr *ctlr;

			ctlr = edev->ctlr;
			if (ctlr->edev == 0)
			{
				/*
		 * Don't let boot process access etherusb until
		 * usbether driver has assigned an address.
		 */
				if (up->pid == 1 && strcmp(up->text, "boot") == 0)
					while (memcmp(edev->ea, nullea, Eaddrlen) == 0)
						tsleep(&up->sleep, return0, 0, 100);
				ctlr->edev = edev;
			}
		}

		static int
			etherusbpnp(Ether * edev)
		{
			Ctlr *ctlr;

			ctlr = malloc(sizeof(Ctlr));
			edev->ctlr = ctlr;
			edev->irq = -1;
			edev->mbps = 100; /* TODO: get this from usbether */

			/*
	 * Linkage to the generic ethernet driver.
	 */
			edev->attach = etherusbattach;
			edev->transmit = etherusbtransmit;
			edev->interrupt = nil;
			edev->ifstat = etherusbifstat;
			edev->ctl = etherusbctl;

			edev->arg = edev;
			/* TODO: promiscuous, multicast (for ipv6), shutdown (for reboot) */
			//	edev->promiscuous = etherusbpromiscuous;
			edev->shutdown = etherusbshutdown;
			edev->multicast = etherusbmulticast;

			return 0;
		}

		void
		etherusblink(void)
		{
			addethercard("usb", etherusbpnp);
		}
		1000XHD = 0x4000,		 /* 1000BASE-X HD Capable */
			Esr1000XFD = 0x8000, /* 1000BASE-X FD Capable */
	};

	typedef struct Mii
	{
		Lock;
		int nphy;
		int mask;
		MiiPhy *phy[NMiiPhy];
		MiiPhy *curphy;

		void *ctlr;
		int (*mir)(Mii *, int, int);
		int (*miw)(Mii *, int, int, int);
	} Mii;

	typedef struct MiiPhy
	{
		Mii *mii;
		u32int id;
		int o
