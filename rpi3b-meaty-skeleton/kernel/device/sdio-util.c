#include<kernel/rpi-base.h>
#include<kernel/systimer.h>
#include<plibc/stdio.h>

#define EMMC_RESP0			((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x300010))
#define EMMCREGS	(PERIPHERAL_BASE+0x300000)

#define Mhz 1000000
enum {
	Extfreq		= 100*Mhz,	/* guess external clock frequency if */
					/* not available from vcore */
	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25*Mhz,	/* standard SD frequency */
	SDfreqhs	= 50*Mhz,	/* high speed frequency */
	DTO		= 14,		/* data timeout exponent (guesswork) */

	GoIdle		= 0,		/* mmc/sdio go idle state */
	MMCSelect	= 7,		/* mmc/sd card select command */
	Setbuswidth	= 6,		/* mmc/sd set bus width command */
	Switchfunc	= 6,		/* mmc/sd switch function command */
	Voltageswitch = 11,		/* md/sdio switch to 1.8V */
	IORWdirect = 52,		/* sdio read/write direct command */
	IORWextended = 53,		/* sdio read/write extended command */
	Appcmd = 55,			/* mmc/sd application command prefix */
};

enum {
	/* Controller registers */
	Arg2			= 0x00>>2,
	Blksizecnt		= 0x04>>2,
	Arg1			= 0x08>>2,
	Cmdtm			= 0x0c>>2,
	Resp0			= 0x10>>2,
	Resp1			= 0x14>>2,
	Resp2			= 0x18>>2,
	Resp3			= 0x1c>>2,
	Data			= 0x20>>2,
	Status			= 0x24>>2,
	Control0		= 0x28>>2,
	Control1		= 0x2c>>2,
	Interrupt		= 0x30>>2,
	Irptmask		= 0x34>>2,
	Irpten			= 0x38>>2,
	Control2		= 0x3c>>2,
	Forceirpt		= 0x50>>2,
	Boottimeout		= 0x70>>2,
	Dbgsel			= 0x74>>2,
	Exrdfifocfg		= 0x80>>2,
	Exrdfifoen		= 0x84>>2,
	Tunestep		= 0x88>>2,
	Tunestepsstd		= 0x8c>>2,
	Tunestepsddr		= 0x90>>2,
	Spiintspt		= 0xf0>>2,
	Slotisrver		= 0xfc>>2,

	/* Control0 */
	Hispeed			= 1<<2,
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,

	/* Control1 */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
	Datatomask		= 0xF0000,
	Clkfreq8shift		= 8,		/* SD clock base divider LSBs */
	Clkfreq8mask		= 0xFF00,
	Clkfreqms2shift		= 6,		/* SD clock base divider MSBs */
	Clkfreqms2mask		= 0xC0,
	Clkgendiv		= 0<<5,		/* SD clock divided */
	Clkgenprog		= 1<<5,		/* SD clock programmable */
	Clken			= 1<<2,		/* SD clock enable */
	Clkstable		= 1<<1,	
	Clkintlen		= 1<<0,		/* enable internal EMMC clocks */

	/* Cmdtm */
	Indexshift		= 24,
	Suspend			= 1<<22,
	Resume			= 2<<22,
	Abort			= 3<<22,
	Isdata			= 1<<21,
	Ixchken			= 1<<20,
	Crcchken		= 1<<19,
	Respmask		= 3<<16,
	Respnone		= 0<<16,
	Resp136			= 1<<16,
	Resp48			= 2<<16,
	Resp48busy		= 3<<16,
	Multiblock		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	Autocmd12		= 1<<2,
	Autocmd23		= 2<<2,
	Blkcnten		= 1<<1,

	/* Interrupt */
	Acmderr		= 1<<24,
	Denderr		= 1<<22,
	Dcrcerr		= 1<<21,
	Dtoerr		= 1<<20,
	Cbaderr		= 1<<19,
	Cenderr		= 1<<18,
	Ccrcerr		= 1<<17,
	Ctoerr		= 1<<16,
	Err		= 1<<15,
	Cardintr	= 1<<8,
	Cardinsert	= 1<<6,		/* not in Broadcom datasheet */
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Bufread		= 1<<11,	/* not in Broadcom datasheet */
	Bufwrite	= 1<<10,	/* not in Broadcom datasheet */
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,
	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,
};

static int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[5]  Resp48,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48busy | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
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
[53] Resp48	| Ixchken | Crcchken | Isdata,
[55] Resp48 | Ixchken | Crcchken,
};

// typedef struct Ctlr Ctlr;

// struct Ctlr {
// 	int	fastclock;
// 	uint32	extclk;
// 	int	appcmd;
// };

// static Ctlr emmc;

static void WR(uint32_t reg, uint32_t val)
{
	uint32_t *r = (uint32_t*)EMMCREGS;
	MicroDelay(20);
	r[reg] = val;
}


#define HZ 100000

void reset_cmd_circuit()
{
	volatile uint32_t *r = (uint32_t*)EMMCREGS;
    if(r[Status] & Cmdinhibit)
    {
		printf("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n", r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd);
        printf("after 0 \n");
		while(r[Status] & Cmdinhibit);
        printf("after 1 \n");
	}
}

uint32_t emmccmd(uint32_t cmd, uint32_t arg, uint32_t *resp)
{
	volatile uint32_t *r;
	uint32_t c;
	uint32_t i;
	uint64_t now;

	r = (uint32_t*)EMMCREGS;
	c = (cmd << Indexshift) | cmdinfo[cmd];

	if(r[Status] & Cmdinhibit)
    {
		printf("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n", r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd);
        printf("after 0 \n");
		while(r[Status] & Cmdinhibit);
        printf("after 1 \n");
	}
	if((r[Status] & Datinhibit) && ((c & Isdata) || (c & Respmask) == Resp48busy))
    {
		printf("emmccmd: need to reset Datinhibit intr %ux stat %ux\n",	r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while(r[Control1] & Srstdata);
        printf("after 3 \n");
		while(r[Status] & Datinhibit);
        printf("after 4 \n");
	}

	WR(Arg1, arg);

	if((i = (r[Interrupt] & ~Cardintr)) != 0)
    {
		if(i != Cardinsert) {
            printf("emmc: before command, intr was %x\n", i);
        }
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = timer_getTickCount64();
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0) {
        if(tick_difference(now, timer_getTickCount64()) > HZ) {
            printf("Breaking after timeout for command done interrupt. \n");
            break;
        }
    }
    // printf("after 5 \n");
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr) {
            printf("emmc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, r[Status]);
        }

		WR(Interrupt, i);

		if(r[Status]&Cmdinhibit){
			WR(Control1, r[Control1]|Srstcmd);
			while(r[Control1]&Srstcmd);
            // printf("after 6 \n");
		}
	}
	WR(Interrupt, i & ~(Datadone|Readrdy|Writerdy));
	switch(c & Respmask){
	case Resp136:
		resp[0] = r[Resp0]<<8;
		resp[1] = r[Resp0]>>24 | r[Resp1]<<8;
		resp[2] = r[Resp1]>>24 | r[Resp2]<<8;
		resp[3] = r[Resp2]>>24 | r[Resp3]<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
        printf("OLD 48 Bit response: %x %x %x %x \n", r[Resp0], r[Resp1], r[Resp2], r[Resp3]);
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		WR(Irpten, r[Irpten]|Datadone|Err);
		// tsleep(&emmc.r, datadone, 0, 3000);
		i = r[Interrupt];
		if((i & Datadone) == 0)
			printf("emmcio: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			printf("emmcio: CMD%d error interrupt %ux\n",
				cmd, r[Interrupt]);
		WR(Interrupt, i);
	}
	return 0;
}

uint32_t sdio_read(uint32_t fn, uint32_t addr) {
	uint32_t resp[4]={0};
	uint32_t r;
	// 0 << 31 signifies the read
	r = emmccmd(IORWdirect, (0<<31) | ((fn&7)<<28) | ((addr&0x1FFFF)<<9), &resp[0]);
	if(r & 0xCF00){
		printf("ether4330: sdiord(%x, %x) fail: %2.2ux %2.2ux\n", fn, addr, (r>>8)&0xFF, r&0xFF);
	}
	return r & 0xFF;
}

void sdio_write(uint32_t fn, uint32_t addr, uint32_t data) {
	uint32_t r;
	uint32_t retry;
	uint32_t resp[4]={0};
	r = 0;
	for(retry = 0; retry < 10; retry++){
		// 1 << 31 signifies the write
		r = emmccmd(IORWdirect, (1<<31)|((fn&7)<<28)|((addr&0x1FFFF)<<9)|(data&0xFF), &resp[0]);
		if((r & 0xCF00) == 0)
			return;
	}
	printf("Err: ether4330: sdiowr(%x, %x, %x) fail: %2.2ux %2.2ux\n", fn, addr, data, (r>>8)&0xFF, r&0xFF);
}

void sdio_set(uint32_t fn, uint32_t addr, uint32_t bits)
{
	sdio_write(fn, addr, sdio_read(fn, addr) | bits);
}

