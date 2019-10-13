arch.c

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"

#include "arm.h"

/*
 * A lot of this stuff doesn't belong here
 * but this is a convenient dumping ground for
 * later sorting into the appropriate buckets.
 */

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
	ureg->r14 = PTR2UINT(sched);
}

/*
 * called in syscallfmt.c, sysfile.c, sysproc.c
 */
void
validalign(uintptr addr, unsigned align)
{
	/*
	 * Plan 9 is a 32-bit O/S, and the hardware it runs on
	 * does not usually have instructions which move 64-bit
	 * quantities directly, synthesizing the operations
	 * with 32-bit move instructions. Therefore, the compiler
	 * (and hardware) usually only enforce 32-bit alignment,
	 * if at all.
	 *
	 * Take this out if the architecture warrants it.
	 */
	if(align == sizeof(vlong))
		align = sizeof(long);

	/*
	 * Check align is a power of 2, then addr alignment.
	 */
	if((align != 0 && !(align & (align-1))) && !(addr & (align-1)))
		return;
	postnote(up, 1, "sys: odd address", NDebug);
	error(Ebadarg);
	/*NOTREACHED*/
}

/* go to user space */
void
kexit(Ureg*)
{
	uvlong t;
	Tos *tos;

	/* precise time accounting, kernel exit */
	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = up->pcycles;
	tos->cyclefreq = m->cpuhz;
	tos->pid = up->pid;

	/* make visible immediately to user proc */
	cachedwbinvse(tos, sizeof *tos);
}

/*
 *  return the userpc the last exception happened at
 */
uintptr
userpc(void)
{
	Ureg *ureg = up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	USED(ureg, pureg, uva, n);
}

/*
 *  this is the body for all kproc's
 */
static void
linkproc(void)
{
	spllo();
	up->kpfun(up->kparg);
	pexit("kproc exiting", 0);
}

/*
 *  setup stack and initial PC for a new kernel proc.  This is architecture
 *  dependent because of the starting stack location
 */
void
kprocchild(Proc *p, void (*func)(void*), void *arg)
{
	p->sched.pc = PTR2UINT(linkproc);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK);

	p->kpfun = func;
	p->kparg = arg;
}

/*
 *  pc output by dumpaproc
 */
uintptr
dbgpc(Proc* p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;

	return ureg->pc;
}

/*
 *  set mach dependent process state for a new process
 */
void
procsetup(Proc* p)
{
	fpusysprocsetup(p);
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc* p)
{
	uvlong t;

	cycles(&t);
	p->pcycles += t;

// TODO: save and restore VFPv3 FP state once 5[cal] know the new registers.
	fpuprocsave(p);
	/*
	 * Prevent the following scenario:
	 *	pX sleeps on cpuA, leaving its page tables in mmul1
	 *	pX wakes up on cpuB, and exits, freeing its page tables
	 *  pY on cpuB allocates a freed page table page and overwrites with data
	 *  cpuA takes an interrupt, and is now running with bad page tables
	 * In theory this shouldn't hurt because only user address space tables
	 * are affected, and mmuswitch will clear mmul1 before a user process is
	 * dispatched.  But empirically it correlates with weird problems, eg
	 * resetting of the core clock at 0x4000001C which confuses local timers.
	 */
	if(conf.nmach > 1)
		mmuswitch(nil);
}

void
procrestore(Proc* p)
{
	uvlong t;

	if(p->kp)
		return;
	cycles(&t);
	p->pcycles -= t;

	fpuprocrestore(p);
}

int
userureg(Ureg* ureg)
{
	return (ureg->psr & PsrMask) == PsrMusr;
}


/*
 * bcm2835 (e.g. original raspberry pi) architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "arm.h"

#include "../port/netif.h"
#include "etherif.h"

#define	POWERREGS	(VIRTIO+0x100000)

Soc soc = {
	.dramsize	= 512*MiB,
	.physio		= 0x20000000,
	.busdram	= 0x40000000,
	.busio		= 0x7E000000,
	.armlocal	= 0,
	.l1ptedramattrs = Cached | Buffered,
	.l2ptedramattrs = Cached | Buffered,
};

enum {
	Wdogfreq	= 65536,
	Wdogtime	= 10,	/* seconds, ≤ 15 */
};

/*
 * Power management / watchdog registers
 */
enum {
	Rstc		= 0x1c>>2,
		Password	= 0x5A<<24,
		CfgMask		= 0x03<<4,
		CfgReset	= 0x02<<4,
	Rsts		= 0x20>>2,
	Wdog		= 0x24>>2,
};

void
archreset(void)
{
	fpon();
}

void
archreboot(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | 1;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
	coherence();
	for(;;)
		;
}

void
wdogfeed(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | (Wdogtime * Wdogfreq);
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
}

void
wdogoff(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask);
}
	
char *
cputype2name(char *buf, int size)
{
	seprint(buf, buf + size, "1176JZF-S");
	return buf;
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	delay(50);				/* let uart catch up */
	print("cpu%d: %dMHz ARM %s\n", m->machno, m->cpumhz, name);
}

int
getncpus(void)
{
	return 1;
}

int
startcpus(uint)
{
	return 1;
}

void
archbcmlink(void)
{
	addclock0link(wdogfeed, HZ);
}

int
archether(unsigned ctlrno, Ether *ether)
{
	ether->type = "usb";
	ether->ctlrno = ctlrno;
	ether->irq = -1;
	ether->nopt = 0;
	return 1;
}

int
l2ap(int ap)
{
	return (AP(3, (ap))|AP(2, (ap))|AP(1, (ap))|AP(0, (ap)));
}

/*
 * atomic ops
 * make sure that we don't drag in the C library versions
 */

long
_xdec(long *p)
{
	int s, v;

	s = splhi();
	v = --*p;
	splx(s);
	return v;
}

void
_xinc(long *p)
{
	int s;

	s = splhi();
	++*p;
	splx(s);
}

int
ainc(int *p)
{
	int s, v;

	s = splhi();
	v = ++*p;
	splx(s);
	return v;
}

int
adec(int *p)
{
	int s, v;

	s = splhi();
	v = --*p;
	splx(s);
	return v;
}

int
cas32(void* addr, u32int old, u32int new)
{
	int r, s;

	s = splhi();
	if(r = (*(u32int*)addr == old))
		*(u32int*)addr = new;
	splx(s);
	if (r)
		coherence();
	return r;
}

int
cmpswap(long *addr, long old, long new)
{
	return cas32(addr, old, new);
}


archbcm2.c

/*
 * bcm2836 (e.g.raspberry pi 2) architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "arm.h"

#include "../port/netif.h"
#include "etherif.h"

typedef struct Mbox Mbox;
typedef struct Mboxes Mboxes;

#define	POWERREGS	(VIRTIO+0x100000)

Soc soc = {
	.dramsize	= 0x3F000000, 	/* was 1024*MiB, but overlaps with physio */
	.physio		= 0x3F000000,
	.busdram	= 0xC0000000,
	.busio		= 0x7E000000,
	.armlocal	= 0x40000000,
	.oscfreq	= 19200000,
	.l1ptedramattrs = Cached | Buffered | L1wralloc | L1sharable,
	.l2ptedramattrs = Cached | Buffered | L2wralloc | L2sharable,
};

enum {
	Wdogfreq	= 65536,
	Wdogtime	= 10,	/* seconds, ≤ 15 */
};

/*
 * Power management / watchdog registers
 */
enum {
	Rstc		= 0x1c>>2,
		Password	= 0x5A<<24,
		CfgMask		= 0x03<<4,
		CfgReset	= 0x02<<4,
	Rsts		= 0x20>>2,
	Wdog		= 0x24>>2,
};

/*
 * Arm local regs for smp
 */
struct Mbox {
	u32int	doorbell;
	u32int	mbox1;
	u32int	mbox2;
	u32int	startcpu;
};
struct Mboxes {
	Mbox	set[4];
	Mbox	clr[4];
};

enum {
	Mboxregs	= 0x80
};

static Lock startlock[MAXMACH + 1];

void
archreset(void)
{
	fpon();
}

void
archreboot(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | 1;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
	coherence();
	for(;;)
		;
}

void
wdogfeed(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | (Wdogtime * Wdogfreq);
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
}

void
wdogoff(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask);
}


char *
cputype2name(char *buf, int size)
{
	u32int r;
	uint part;
	char *p;

	r = cpidget();			/* main id register */
	assert((r >> 24) == 'A');
	part = (r >> 4) & MASK(12);
	switch(part){
	case 0xc07:
		p = seprint(buf, buf + size, "Cortex-A7");
		break;
	case 0xd03:
		p = seprint(buf, buf + size, "Cortex-A53");
		break;
	case 0xd08:
		p = seprint(buf, buf + size, "Cortex-A72");
		break;
	default:
		p = seprint(buf, buf + size, "Unknown-%#x", part);
		break;
	}
	seprint(p, buf + size, " r%ldp%ld",
		(r >> 20) & MASK(4), r & MASK(4));
	return buf;
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	delay(50);				/* let uart catch up */
	print("cpu%d: %dMHz ARM %s\n", m->machno, m->cpumhz, name);
}

int
getncpus(void)
{
	int n, max;
	char *p;

	n = 4;
	if(n > MAXMACH)
		n = MAXMACH;
	p = getconf("*ncpu");
	if(p && (max = atoi(p)) > 0 && n > max)
		n = max;
	return n;
}

static int
startcpu(uint cpu)
{
	Mboxes *mb;
	int i;
	void cpureset();

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	if(mb->clr[cpu].startcpu)
		return -1;
	mb->set[cpu].startcpu = PADDR(cpureset);
	coherence();
	sev();
	for(i = 0; i < 1000; i++)
		if(mb->clr[cpu].startcpu == 0)
			return 0;
	mb->clr[cpu].startcpu = PADDR(cpureset);
	mb->set[cpu].doorbell = 1;
	return 0;
}

void
mboxclear(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->clr[cpu].mbox1 = 1;
}

void
wakecpu(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->set[cpu].mbox1 = 1;
}

int
startcpus(uint ncpu)
{
	int i, timeout;

	for(i = 0; i < ncpu; i++)
		lock(&startlock[i]);
	cachedwbse(startlock, sizeof startlock);
	for(i = 1; i < ncpu; i++){
		if(startcpu(i) < 0)
			return i;
		timeout = 10000000;
		while(!canlock(&startlock[i]))
			if(--timeout == 0)
				return i;
		unlock(&startlock[i]);
	}
	return ncpu;
}

void
archbcm2link(void)
{
	addclock0link(wdogfeed, HZ);
}

int
archether(unsigned ctlrno, Ether *ether)
{
	switch(ctlrno){
	case 0:
		ether->type = "usb";
		break;
	case 1:
		ether->type = "4330";
		break;
	default:
		return 0;
	}
	ether->ctlrno = ctlrno;
	ether->irq = -1;
	ether->nopt = 0;
	ether->maxmtu = 9014;
	return 1;
}

int
l2ap(int ap)
{
	return (AP(0, (ap)));
}

int
cmpswap(long *addr, long old, long new)
{
	return cas((ulong*)addr, old, new);
}

void
cpustart(int cpu)
{
	Mboxes *mb;
	void machon(int);

	up = nil;
	machinit();
	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->clr[cpu].doorbell = 1;
	trapinit();
	clockinit();
	mmuinit1();
	timersinit();
	cpuidprint();
	archreset();
	machon(m->machno);
	unlock(&startlock[cpu]);
	schedinit();
	panic("schedinit returned");
}
id)
{
	Ureg *ureg = up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	USED(ureg, pureg, uva, n);
}

/*
 *  this is the body for all kproc's
 */
archbcm4.c

/*
 * bcm2711 (raspberry pi 4) architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "arm.h"

#include "../port/netif.h"
#include "etherif.h"

typedef struct Mbox Mbox;
typedef struct Mboxes Mboxes;

#define	POWERREGS	(VIRTIO+0x100000)

Soc soc = {
	.dramsize	= 0x7E000000,
	.physio		= 0xFE000000,
	.busdram	= 0xC0000000,
	.busio		= 0x7E000000,
	.armlocal	= 0xFF800000,
	.oscfreq	= 54000000,
	.l1ptedramattrs = Cached | Buffered | L1wralloc | L1sharable,
	.l2ptedramattrs = Cached | Buffered | L2wralloc | L2sharable,
};

enum {
	Wdogfreq	= 65536,
	Wdogtime	= 10,	/* seconds, ≤ 15 */
};

/*
 * Power management / watchdog registers
 */
enum {
	Rstc		= 0x1c>>2,
		Password	= 0x5A<<24,
		CfgMask		= 0x03<<4,
		CfgReset	= 0x02<<4,
	Rsts		= 0x20>>2,
	Wdog		= 0x24>>2,
};

/*
 * Arm local regs for smp
 */
struct Mbox {
	u32int	doorbell;
	u32int	mbox1;
	u32int	mbox2;
	u32int	startcpu;
};
struct Mboxes {
	Mbox	set[4];
	Mbox	clr[4];
};

enum {
	Mboxregs	= 0x80
};

static Lock startlock[MAXMACH + 1];

void
archreset(void)
{
	fpon();
}

void
archreboot(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | 1;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
	coherence();
	for(;;)
		;
}

void
wdogfeed(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | (Wdogtime * Wdogfreq);
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
}

void
wdogoff(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask);
}


char *
cputype2name(char *buf, int size)
{
	u32int r;
	uint part;
	char *p;

	r = cpidget();			/* main id register */
	assert((r >> 24) == 'A');
	part = (r >> 4) & MASK(12);
	switch(part){
	case 0xc07:
		p = seprint(buf, buf + size, "Cortex-A7");
		break;
	case 0xd03:
		p = seprint(buf, buf + size, "Cortex-A53");
		break;
	case 0xd08:
		p = seprint(buf, buf + size, "Cortex-A72");
		break;
	default:
		p = seprint(buf, buf + size, "Unknown-%#x", part);
		break;
	}
	seprint(p, buf + size, " r%ldp%ld",
		(r >> 20) & MASK(4), r & MASK(4));
	return buf;
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	delay(50);				/* let uart catch up */
	print("cpu%d: %dMHz ARM %s\n", m->machno, m->cpumhz, name);
}

int
getncpus(void)
{
	int n, max;
	char *p;

	n = 4;
	if(n > MAXMACH)
		n = MAXMACH;
	p = getconf("*ncpu");
	if(p && (max = atoi(p)) > 0 && n > max)
		n = max;
	return n;
}

static int
startcpu(uint cpu)
{
	Mboxes *mb;
	int i;
	void cpureset();

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	if(mb->clr[cpu].startcpu)
		return -1;
	mb->set[cpu].startcpu = PADDR(cpureset);
	coherence();
	sev();
	for(i = 0; i < 1000; i++)
		if(mb->clr[cpu].startcpu == 0)
			return 0;
	mb->clr[cpu].startcpu = PADDR(cpureset);
	mb->set[cpu].doorbell = 1;
	return 0;
}

void
mboxclear(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->clr[cpu].mbox1 = 1;
}

void
wakecpu(uint cpu)
{
	Mboxes *mb;

	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->set[cpu].mbox1 = 1;
}

int
startcpus(uint ncpu)
{
	int i, timeout;

	for(i = 0; i < ncpu; i++)
		lock(&startlock[i]);
	cachedwbse(startlock, sizeof startlock);
	for(i = 1; i < ncpu; i++){
		if(startcpu(i) < 0)
			return i;
		timeout = 10000000;
		while(!canlock(&startlock[i]))
			if(--timeout == 0)
				return i;
		unlock(&startlock[i]);
	}
	return ncpu;
}

void
archbcm4link(void)
{
	addclock0link(wdogfeed, HZ);
}

int
archether(unsigned ctlrno, Ether *ether)
{
	switch(ctlrno){
	case 0:
		ether->type = "genet";
		break;
	case 1:
		ether->type = "4330";
		break;
	default:
		return 0;
	}
	ether->ctlrno = ctlrno;
	ether->irq = -1;
	ether->nopt = 0;
	ether->maxmtu = 9014;
	return 1;
}

int
l2ap(int ap)
{
	return (AP(0, (ap)));
}

int
cmpswap(long *addr, long old, long new)
{
	return cas((ulong*)addr, old, new);
}

void
cpustart(int cpu)
{
	Mboxes *mb;
	void machon(int);

	up = nil;
	machinit();
	mb = (Mboxes*)(ARMLOCAL + Mboxregs);
	mb->clr[cpu].doorbell = 1;
	trapinit();
	clockinit();
	mmuinit1();
	timersinit();
	cpuidprint();
	archreset();
	machon(m->machno);
	unlock(&startlock[cpu]);
	schedinit();
	panic("schedinit returned");
}

	return v;
}

int
adec(int *p)
{
	int s, v;

	s = splhi();
	v = --*p;
	splx(s);
	return v;
}

int
cas32(void* addr, u32int old, u32int new)
{
	int r, s;

	s = splhi();
	if(r = (*(u32int*)addr == old))
		*(u32int*)addr = new;
	splx(s);
	if (r)
		coherence();
	return r;
}

int
cmpswap(long *addr, long old, long new)
{
	return cas32(addr, old, new);
}

arm.h
/*
 * arm-specific definitions for armv6 (arm11), armv7 (cortex-a8 and -a7)
 * these are used in C and assembler
 */

/*
 * Program Status Registers
 */
#define PsrMusr		0x00000010		/* mode */
#define PsrMfiq		0x00000011
#define PsrMirq		0x00000012
#define PsrMsvc		0x00000013	/* `protected mode for OS' */
#define PsrMmon		0x00000016	/* `secure monitor' (trustzone hyper) */
#define PsrMabt		0x00000017
#define PsrMhyp		0x0000001A
#define PsrMund		0x0000001B
#define PsrMsys		0x0000001F	/* `privileged user mode for OS' (trustzone) */
#define PsrMask		0x0000001F

#define PsrDfiq		0x00000040		/* disable FIQ interrupts */
#define PsrDirq		0x00000080		/* disable IRQ interrupts */

#define PsrV		0x10000000		/* overflow */
#define PsrC		0x20000000		/* carry/borrow/extend */
#define PsrZ		0x40000000		/* zero */
#define PsrN		0x80000000		/* negative/less than */

/* instruction decoding */
#define ISCPOP(op)	((op) == 0xE || ((op) & ~1) == 0xC)
#define ISFPAOP(cp, op)	((cp) == CpOFPA && ISCPOP(op))
#define ISVFPOP(cp, op)	(((cp) == CpDFP || (cp) == CpFP) && ISCPOP(op))

/*
 * Coprocessors
 */
#define CpOFPA		1			/* ancient 7500 FPA */
#define CpFP		10			/* float FP, VFP cfg. */
#define CpDFP		11			/* double FP */
#define CpSC		15			/* System Control */

/*
 * Primary (CRn) CpSC registers.
 */
#define	CpID		0			/* ID and cache type */
#define	CpCONTROL	1			/* miscellaneous control */
#define	CpTTB		2			/* Translation Table Base(s) */
#define	CpDAC		3			/* Domain Access Control */
#define	CpFSR		5			/* Fault Status */
#define	CpFAR		6			/* Fault Address */
#define	CpCACHE		7			/* cache/write buffer control */
#define	CpTLB		8			/* TLB control */
#define	CpCLD		9			/* L2 Cache Lockdown, op1==1 */
#define CpTLD		10			/* TLB Lockdown, with op2 */
#define CpVECS		12			/* vector bases, op1==0, Crm==0, op2s (cortex) */
#define	CpPID		13			/* Process ID */
#define	CpTIMER		14			/* Generic timer (cortex-a7) */
#define CpSPM		15			/* system performance monitor (arm1176) */

/*
 * CpTIMER op1==0 Crm and opcode2 registers (cortex-a7)
 */
#define	CpTIMERcntfrq	0
#define CpTIMERphys		2

#define CpTIMERphysval	0
#define CpTIMERphysctl	1

/*
 * CpTTB op1==0, Crm==0 opcode2 values.
 */
#define CpTTB0		0
#define CpTTB1		1			/* cortex */
#define CpTTBctl	2			/* cortex */

/*
 * CpFSR opcode2 values.
 */
#define	CpFSRdata	0			/* armv6, armv7 */
#define	CpFSRinst	1			/* armv6, armv7 */

/*
 * CpID Secondary (CRm) registers.
 */
#define CpIDidct	0
#define	CpIDfeat	1

/*
 * CpID op1==0 opcode2 fields.
 * the cortex has more op1 codes for cache size, etc.
 */
#define CpIDid		0			/* main ID */
#define CpIDct		1			/* cache type */
#define CpIDtlb		3			/* tlb type (cortex) */
#define CpIDmpid	5			/* multiprocessor id (cortex) */
#define	CpIDrevid	6			/* extra revision ID */

/* CpIDid op1 values */
#define CpIDcsize	1			/* cache size (cortex) */
#define CpIDcssel	2			/* cache size select (cortex) */

/*
 * CpCONTROL op2 codes, op1==0, Crm==0.
 */
#define CpMainctl	0
#define CpAuxctl	1
#define CpCPaccess	2

/*
 * CpCONTROL: op1==0, CRm==0, op2==CpMainctl.
 * main control register.
 * cortex/armv7 has more ops and CRm values.
 */
#define CpCmmu		0x00000001	/* M: MMU enable */
#define CpCalign	0x00000002	/* A: alignment fault enable */
#define CpCdcache	0x00000004	/* C: data cache on */
#define CpCsbo (3<<22|1<<18|1<<16|017<<3)	/* must be 1 (armv7) */
#define CpCsbz (CpCtre|1<<26|CpCve|1<<15|7<<7)	/* must be 0 (armv7) */
#define CpCsw		(1<<10)		/* SW: SWP(B) enable (deprecated in v7) */
#define CpCpredict	0x00000800	/* Z: branch prediction (armv7) */
#define CpCicache	0x00001000	/* I: instruction cache on */
#define CpChv		0x00002000	/* V: high vectors */
#define CpCrr		(1<<14)	/* RR: round robin vs random cache replacement */
#define CpCha		(1<<17)		/* HA: hw access flag enable */
#define CpCdz		(1<<19)		/* DZ: divide by zero fault enable */
#define CpCfi		(1<<21)		/* FI: fast intrs */
#define CpCve		(1<<24)		/* VE: intr vectors enable */
#define CpCee		(1<<25)		/* EE: exception endianness */
#define CpCnmfi		(1<<27)		/* NMFI: non-maskable fast intrs. */
#define CpCtre		(1<<28)		/* TRE: TEX remap enable */
#define CpCafe		(1<<29)		/* AFE: access flag (ttb) enable */

/*
 * CpCONTROL: op1==0, CRm==0, op2==CpAuxctl.
 * Auxiliary control register on cortex at least.
 */
#define CpACcachenopipe		(1<<20)	/* don't pipeline cache maint. */
#define CpACcp15serial		(1<<18)	/* serialise CP1[45] ops. */
#define CpACcp15waitidle	(1<<17)	/* CP1[45] wait-on-idle */
#define CpACcp15pipeflush	(1<<16)	/* CP1[45] flush pipeline */
#define CpACneonissue1		(1<<12)	/* neon single issue */
#define CpACldstissue1		(1<<11)	/* force single issue ld, st */
#define CpACissue1		(1<<10)	/* force single issue */
#define CpACnobsm		(1<<7)	/* no branch size mispredicts */
#define CpACibe			(1<<6)	/* cp15 invalidate & btb enable */
#define CpACl1neon		(1<<5)	/* cache neon (FP) data in L1 cache */
#define CpACasa			(1<<4)	/* enable speculative accesses */
#define CpACl1pe		(1<<3)	/* l1 cache parity enable */
#define CpACl2en		(1<<1)	/* l2 cache enable; default 1 */

/* cortex-a7 and cortex-a9 */
#define CpACsmp			(1<<6)	/* SMP l1 caches coherence; needed for ldrex/strex */
#define CpACl1pctl		(3<<13)	/* l1 prefetch control */
/*
 * CpCONTROL Secondary (CRm) registers and opcode2 fields.
 */
#define CpCONTROLscr	1

#define CpSCRscr	0

/*
 * CpCACHE Secondary (CRm) registers and opcode2 fields.  op1==0.
 * In ARM-speak, 'flush' means invalidate and 'clean' means writeback.
 */
#define CpCACHEintr	0			/* interrupt (op2==4) */
#define CpCACHEisi	1			/* inner-sharable I cache (v7) */
#define CpCACHEpaddr	4			/* 0: phys. addr (cortex) */
#define CpCACHEinvi	5			/* instruction, branch table */
#define CpCACHEinvd	6			/* data or unified */
#define CpCACHEinvu	7			/* unified (not on cortex) */
#define CpCACHEva2pa	8			/* va -> pa translation (cortex) */
#define CpCACHEwb	10			/* writeback to PoC */
#define CpCACHEwbu	11			/* writeback to PoU */
#define CpCACHEwbi	14			/* writeback+invalidate (to PoC) */

#define CpCACHEall	0			/* entire (not for invd nor wb(i) on cortex) */
#define CpCACHEse	1			/* single entry */
#define CpCACHEsi	2			/* set/index (set/way) */
#define CpCACHEtest	3			/* test loop */
#define CpCACHEwait	4			/* wait (prefetch flush on cortex) */
#define CpCACHEdmbarr	5			/* wb only (cortex) */
#define CpCACHEflushbtc	6			/* flush branch-target cache (cortex) */
#define CpCACHEflushbtse 7			/* ⋯ or just one entry in it (cortex) */

/*
 * CpTLB Secondary (CRm) registers and opcode2 fields.
 */
#define CpTLBinvi	5			/* instruction */
#define CpTLBinvd	6			/* data */
#define CpTLBinvu	7			/* unified */

#define CpTLBinv	0			/* invalidate all */
#define CpTLBinvse	1			/* invalidate single entry */
#define CpTBLasid	2			/* by ASID (cortex) */

/*
 * CpCLD Secondary (CRm) registers and opcode2 fields for op1==0. (cortex)
 */
#define CpCLDena	12			/* enables */
#define CpCLDcyc	13			/* cycle counter */
#define CpCLDuser	14			/* user enable */

#define CpCLDenapmnc	0
#define CpCLDenacyc	1

/*
 * CpCLD Secondary (CRm) registers and opcode2 fields for op1==1.
 */
#define CpCLDl2		0			/* l2 cache */

#define CpCLDl2aux	2			/* auxiliary control */

/*
 * l2 cache aux. control
 */
#define CpCl2ecc	(1<<28)			/* use ecc, not parity */
#define CpCl2noldforw	(1<<27)			/* no ld forwarding */
#define CpCl2nowrcomb	(1<<25)			/* no write combining */
#define CpCl2nowralldel	(1<<24)			/* no write allocate delay */
#define CpCl2nowrallcomb (1<<23)		/* no write allocate combine */
#define CpCl2nowralloc	(1<<22)			/* no write allocate */
#define CpCl2eccparity	(1<<21)			/* enable ecc or parity */
#define CpCl2inner	(1<<16)			/* inner cacheability */
/* other bits are tag ram & data ram latencies */

/*
 * CpTLD Secondary (CRm) registers and opcode2 fields.
 */
#define CpTLDlock	0			/* TLB lockdown registers */
#define CpTLDpreload	1			/* TLB preload */

#define CpTLDi		0			/* TLB instr. lockdown reg. */
#define CpTLDd		1			/* " data " " */

/*
 * CpVECS Secondary (CRm) registers and opcode2 fields.
 */
#define CpVECSbase	0

#define CpVECSnorm	0			/* (non-)secure base addr */
#define CpVECSmon	1			/* secure monitor base addr */

/*
 * CpSPM Secondary (CRm) registers and opcode2 fields (armv6)
 */
#define CpSPMperf	12			/* various counters */

#define CpSPMctl	0			/* performance monitor control */
#define	CpSPMcyc	1			/* cycle counter register */

/*
 * CpCACHERANGE opcode2 fields for MCRR instruction (armv6)
 */
#define	CpCACHERANGEinvi	5		/* invalidate instruction  */
#define	CpCACHERANGEinvd	6		/* invalidate data */
#define CpCACHERANGEdwb		12		/* writeback */
#define CpCACHERANGEdwbi	14		/* writeback+invalidate */

/*
 * CpTTB cache control bits
 */
#define CpTTBnos	(1<<5)	/* only Inner cache shareable */
#define CpTTBinc	(0<<0|0<<6)	/* inner non-cacheable */
#define CpTTBiwba	(0<<0|1<<6)	/* inner write-back write-allocate */
#define CpTTBiwt	(1<<0|0<<6)	/* inner write-through */
#define CpTTBiwb	(1<<0|1<<6)	/* inner write-back no write-allocate */
#define CpTTBonc	(0<<3)	/* outer non-cacheable */
#define CpTTBowba	(1<<3)	/* outer write-back write-allocate */
#define CpTTBowt	(2<<3)	/* outer write-through */
#define CpTTBowb	(3<<3)	/* outer write-back no write-allocate */
#define CpTTBs	(1<<1)	/* page table in shareable memory */
#define CpTTBbase	~0x7F		/* mask off control bits */

/*
 * MMU page table entries.
 * Mbz (0x10) bit is implementation-defined and must be 0 on the cortex.
 */
#define Mbz		(0<<4)
#define Fault		0x00000000		/* L[12] pte: unmapped */

#define Coarse		(Mbz|1)			/* L1 */
#define Section		(Mbz|2)			/* L1 1MB */
#define Fine		(Mbz|3)			/* L1 */

#define Large		0x00000001		/* L2 64KB */
#define Small		0x00000002		/* L2 4KB */
#define Tiny		0x00000003		/* L2 1KB: not in v7 */
#define Buffered	0x00000004		/* L[12]: write-back not -thru */
#define Cached		0x00000008		/* L[12] */
#define Dom0		0

#define L1wralloc	(1<<12)			/* L1 TEX */
#define L1sharable	(1<<16)
#define	L1noexec	(1<<4)
#define L2wralloc	(1<<6)			/* L2 TEX (small pages) */
#define L2sharable	(1<<10)

/* attributes for memory containing locks -- differs between armv6 and armv7 */
//#define L1ptedramattrs	(Cached | Buffered | L1wralloc | L1sharable)
//#define L2ptedramattrs	(Cached | Buffered | L2wralloc | L2sharable)

#define Noaccess	0			/* AP, DAC */
#define Krw		1			/* AP */
/* armv7 deprecates AP[2] == 1 & AP[1:0] == 2 (Uro), prefers 3 (new in v7) */
#define Uro		2			/* AP */
#define Urw		3			/* AP */
#define Client		1			/* DAC */
#define Manager		3			/* DAC */

#define F(v, o, w)	(((v) & ((1<<(w))-1))<<(o))
#define AP(n, v)	F((v), ((n)*2)+4, 2)
#define L1AP(ap)	(AP(3, (ap)))
/* L2AP differs between armv6 and armv7 -- see l2ap in arch*.c */
#define DAC(n, v)	F((v), (n)*2, 2)

#define HVECTORS	0xffff0000
efine PsrDfiq		0x00000040		/* disable FIQ interrupts */
#define PsrDirq		0x00000080		/* disable IRQ interrupts */

#define PsrV		0x10000000		/* overflow */
#define PsrC		0x20000000		/* carry/borrow/extend */
#define PsrZ		0x40000000		/* zero */
#define PsrN		0x80000000		/* negative/less than */

/* instruction decoding */
#define ISCPOP(op)	((op) == 0xE || ((op) & ~1) == 0xC)
#define ISFPAOP(cp, op)	((cp) == CpOFPA && ISCPOP(op))
#define ISVFPOP(cp, op)arm.s                                                                                                  664       0       0         2606 12666536044  10175                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * armv6/v7 machine assist, definitions
 *
 * loader uses R11 as scratch.
 */

#include "mem.h"
#include "arm.h"

#define PADDR(va)	(PHYSDRAM | ((va) & ~KSEGM))

#define L1X(va)		(((((va))>>20) & 0x0fff)<<2)

/*
 * new instructions
 */

#define ISB	\
	MOVW	$0, R0; \
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEwait

#define DSB \
	MOVW	$0, R0; \
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEwait

#define	BARRIERS	DSB; ISB

#define MCRR(coproc, op, rd, rn, crm) \
	WORD $(0xec400000|(rn)<<16|(rd)<<12|(coproc)<<8|(op)<<4|(crm))
#define MRRC(coproc, op, rd, rn, crm) \
	WORD $(0xec500000|(rn)<<16|(rd)<<12|(coproc)<<8|(op)<<4|(crm))
#define MSR(R, rn, m, m1) \
	WORD $(0xe120f200|(R)<<22|(m1)<<16|(m)<<8|(rn))

#define	LDREX(fp,t)   WORD $(0xe<<28|0x01900f9f | (fp)<<16 | (t)<<12)
/* `The order of operands is from left to right in dataflow order' - asm man */
#define	STREX(f,tp,r) WORD $(0xe<<28|0x01800f90 | (tp)<<16 | (r)<<12 | (f)<<0)
#define	CLREX	WORD	$0xf57ff01f

#define CPSIE	WORD	$0xf1080080	/* intr enable: zeroes I bit */
#define CPSID	WORD	$0xf10c0080	/* intr disable: sets I bit */

#define OKAY \
	MOVW	$0x7E200028,R2; \
	MOVW	$0x10000,R3; \
	MOVW	R3,(R2)

#define PUTC(s)

/*
 * get cpu id, or zero if armv6
 */
#define CPUID(r) \
	MRC	CpSC, 0, r, C(CpID), C(CpIDfeat), 7; \
	CMP	$0, r; \
	B.EQ	2(PC); \
	MRC	CpSC, 0, r, C(CpID), C(CpIDidct), CpIDmpid; \
	AND.S	$(MAXMACH-1), r

m==0.
 */
#define CpMainctl	0
#define CpAuxctl	1
#define CpCPaccess	2

/*
 * CpCONTROL: op1==0, CRm==0, op2==CpMainctl.
 *armv6.s
 /*
 * Broadcom bcm2835 SoC, as used in Raspberry Pi
 * arm1176jzf-s processor (armv6)
 */

#include "arm.s"

#define CACHELINESZ 32

TEXT armstart(SB), 1, $-4

	/*
	 * SVC mode, interrupts disabled
	 */
	MOVW	$(PsrDirq|PsrDfiq|PsrMsvc), R1
	MOVW	R1, CPSR

	/*
	 * disable the mmu and L1 caches
	 * invalidate caches and tlb
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCpredict|CpCmmu), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvu), CpCACHEall
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	ISB

	/*
	 * clear mach and page tables
	 */
	MOVW	$PADDR(MACHADDR), R1
	MOVW	$PADDR(KTZERO), R2
_ramZ:
	MOVW	R0, (R1)
	ADD	$4, R1
	CMP	R1, R2
	BNE	_ramZ

	/*
	 * start stack at top of mach (physical addr)
	 * set up page tables for kernel
	 */
	MOVW	$PADDR(MACHADDR+MACHSIZE-4), R13
	MOVW	$PADDR(L1), R0
	BL	,mmuinit(SB)

	/*
	 * set up domain access control and page table base
	 */
	MOVW	$Client, R1
	MCR	CpSC, 0, R1, C(CpDAC), C(0)
	MOVW	$PADDR(L1), R1
	MCR	CpSC, 0, R1, C(CpTTB), C(0)

	/*
	 * enable caches, mmu, and high vectors
	 */
	MRC	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpChv|CpCdcache|CpCicache|CpCpredict|CpCmmu), R0
	MCR	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	ISB

	/*
	 * switch SB, SP, and PC into KZERO space
	 */
	MOVW	$setR12(SB), R12
	MOVW	$(MACHADDR+MACHSIZE-4), R13
	MOVW	$_startpg(SB), R15

TEXT _startpg(SB), 1, $-4

	/*
	 * enable cycle counter
	 */
	MOVW	$1, R1
	MCR	CpSC, 0, R1, C(CpSPM), C(CpSPMperf), CpSPMctl

	/*
	 * call main and loop forever if it returns
	 */
	BL	,main(SB)
	B	,0(PC)

	BL	_div(SB)		/* hack to load _div, etc. */

TEXT cpidget(SB), 1, $-4			/* main ID */
	MRC	CpSC, 0, R0, C(CpID), C(0), CpIDid
	RET

TEXT fsrget(SB), 1, $-4				/* data fault status */
	MRC	CpSC, 0, R0, C(CpFSR), C(0), CpFSRdata
	RET

TEXT ifsrget(SB), 1, $-4			/* instruction fault status */
	MRC	CpSC, 0, R0, C(CpFSR), C(0), CpFSRinst
	RET

TEXT farget(SB), 1, $-4				/* fault address */
	MRC	CpSC, 0, R0, C(CpFAR), C(0x0)
	RET

TEXT lcycles(SB), 1, $-4
	MRC	CpSC, 0, R0, C(CpSPM), C(CpSPMperf), CpSPMcyc
	RET

TEXT splhi(SB), 1, $-4
	MOVW	$(MACHADDR+4), R2		/* save caller pc in Mach */
	MOVW	R14, 0(R2)

	MOVW	CPSR, R0			/* turn off irqs (but not fiqs) */
	ORR	$(PsrDirq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splfhi(SB), 1, $-4
	MOVW	$(MACHADDR+4), R2		/* save caller pc in Mach */
	MOVW	R14, 0(R2)

	MOVW	CPSR, R0			/* turn off irqs and fiqs */
	ORR	$(PsrDirq|PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splflo(SB), 1, $-4
	MOVW	CPSR, R0			/* turn on fiqs */
	BIC	$(PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT spllo(SB), 1, $-4
	MOVW	CPSR, R0			/* turn on irqs and fiqs */
	BIC	$(PsrDirq|PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splx(SB), 1, $-4
	MOVW	$(MACHADDR+0x04), R2		/* save caller pc in Mach */
	MOVW	R14, 0(R2)

	MOVW	R0, R1				/* reset interrupt level */
	MOVW	CPSR, R0
	MOVW	R1, CPSR
	RET

TEXT spldone(SB), 1, $0				/* end marker for devkprof.c */
	RET

TEXT islo(SB), 1, $-4
	MOVW	CPSR, R0
	AND	$(PsrDirq), R0
	EOR	$(PsrDirq), R0
	RET

TEXT	tas(SB), $-4
TEXT	_tas(SB), $-4
	MOVW	R0,R1
	MOVW	$1,R0
	SWPW	R0,(R1)			/* fix: deprecated in armv6 */
	RET

TEXT setlabel(SB), 1, $-4
	MOVW	R13, 0(R0)		/* sp */
	MOVW	R14, 4(R0)		/* pc */
	MOVW	$0, R0
	RET

TEXT gotolabel(SB), 1, $-4
	MOVW	0(R0), R13		/* sp */
	MOVW	4(R0), R14		/* pc */
	MOVW	$1, R0
	RET

TEXT getcallerpc(SB), 1, $-4
	MOVW	0(R13), R0
	RET

TEXT idlehands(SB), $-4
	MOVW	CPSR, R3
	ORR	$(PsrDirq|PsrDfiq), R3, R1		/* splfhi */
	MOVW	R1, CPSR

	DSB
	MOVW	nrdy(SB), R0
	CMP	$0, R0
	MCR.EQ	CpSC, 0, R0, C(CpCACHE), C(CpCACHEintr), CpCACHEwait
	DSB

	MOVW	R3, CPSR			/* splx */
	RET


TEXT coherence(SB), $-4
	BARRIERS
	RET

/*
 * invalidate tlb
 */
TEXT mmuinvalidate(SB), 1, $-4
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS
	MCR CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEflushbtc
	RET

/*
 * mmuinvalidateaddr(va)
 *   invalidate tlb entry for virtual page address va, ASID 0
 */
TEXT mmuinvalidateaddr(SB), 1, $-4
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinvse
	BARRIERS
	RET

/*
 * drain write buffer
 * writeback data cache
 */
TEXT cachedwb(SB), 1, $-4
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEall
	RET

/*
 * drain write buffer
 * writeback and invalidate data cache
 */
TEXT cachedwbinv(SB), 1, $-4
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwbi), CpCACHEall
	RET

/*
 * cachedwbinvse(va, n)
 *   drain write buffer
 *   writeback and invalidate data cache range [va, va+n)
 */
TEXT cachedwbinvse(SB), 1, $-4
	MOVW	R0, R1		/* DSB clears R0 */
	DSB
	MOVW	n+4(FP), R2
	ADD	R1, R2
	SUB	$1, R2
	BIC	$(CACHELINESZ-1), R1
	BIC	$(CACHELINESZ-1), R2
	MCRR(CpSC, 0, 2, 1, CpCACHERANGEdwbi)
	RET

/*
 * cachedwbse(va, n)
 *   drain write buffer
 *   writeback data cache range [va, va+n)
 */
TEXT cachedwbtlb(SB), 1, $-4
TEXT cachedwbse(SB), 1, $-4

	MOVW	R0, R1		/* DSB clears R0 */
	DSB
	MOVW	n+4(FP), R2
	ADD	R1, R2
	BIC	$(CACHELINESZ-1), R1
	BIC	$(CACHELINESZ-1), R2
	MCRR(CpSC, 0, 2, 1, CpCACHERANGEdwb)
	RET

/*
 * cachedinvse(va, n)
 *   drain write buffer
 *   invalidate data cache range [va, va+n)
 */
TEXT cachedinvse(SB), 1, $-4
	MOVW	R0, R1		/* DSB clears R0 */
	DSB
	MOVW	n+4(FP), R2
	ADD	R1, R2
	SUB	$1, R2
	BIC	$(CACHELINESZ-1), R1
	BIC	$(CACHELINESZ-1), R2
	MCRR(CpSC, 0, 2, 1, CpCACHERANGEinvd)
	RET

/*
 * drain write buffer and prefetch buffer
 * writeback and invalidate data cache
 * invalidate instruction cache
 */
TEXT cacheuwbinv(SB), 1, $-4
	BARRIERS
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwbi), CpCACHEall
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall
	RET

/*
 * L2 cache is not enabled
 */
TEXT l2cacheuwbinv(SB), 1, $-4
	RET

/*
 * invalidate instruction cache
 */
TEXT cacheiinv(SB), 1, $-4
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall
	RET

/*
 * invalidate range of instruction cache
 */
TEXT cacheiinvse(SB), 1, $-4
	MOVW	R0, R1		/* DSB clears R0 */
	DSB
	MOVW n+4(FP), R2
	ADD	R1, R2
	SUB	$1, R2
	MCRR(CpSC, 0, 2, 1, CpCACHERANGEinvi)
	MCR CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEflushbtc
	DSB
	ISB
	RET
ny		0x00000003		/* L2 1KB: not in v7 */
#define Buffered	0x00000004		/* L[12]: write-back not -thru */
#define Cached		0x00000008		/* L[12] */
#define Dom0		0

#define L1wralloc	(1<<12)			/* L1 TEX */
#define L1sharable	(1<<16)
#define	L1noexec	(1<<4)
#define L2wralloc	(1<<6)			/* L2 TEX (small pages) */
#define L2sharable	(1<<10)

/* attributes for memory containing locks -- differs between armv6 and armv7 */
//#define L1ptedramattrs	(Cached | Buffered | L1wralloc | L1sharable)
//#armv7.s
/*
 * Broadcom bcm2836 SoC, as used in Raspberry Pi 2
 * 4 x Cortex-A7 processor (armv7)
 */

#include "arm.s"

#define CACHELINESZ 	64
#define ICACHELINESZ	32

#undef DSB
#undef DMB
#undef ISB
#define DSB	WORD	$0xf57ff04f	/* data synch. barrier; last f = SY */
#define DMB	WORD	$0xf57ff05f	/* data mem. barrier; last f = SY */
#define ISB	WORD	$0xf57ff06f	/* instr. sync. barrier; last f = SY */
#define WFI	WORD	$0xe320f003	/* wait for interrupt */
#define WFI_EQ	WORD	$0x0320f003	/* wait for interrupt if eq */
#define ERET	WORD	$0xe160006e	/* exception return from HYP */
#define SEV	WORD	$0xe320f004	/* send event */

/* tas/cas strex debugging limits; started at 10000 */
#define MAXSC 1000000

TEXT armstart(SB), 1, $-4

	/*
	 * if not cpu0, go to secondary startup
	 */
	CPUID(R1)
	BNE	reset

	/*
	 * go to SVC mode, interrupts disabled
	 */
	BL	svcmode(SB)

	/*
	 * disable the mmu and caches
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCmmu), R1
	ORR	$(CpCsbo|CpCsw), R1
	BIC	$CpCsbz, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/*
	 * clear mach and page tables
	 */
	MOVW	$PADDR(MACHADDR), R1
	MOVW	$PADDR(KTZERO), R2
_ramZ:
	MOVW	R0, (R1)
	ADD	$4, R1
	CMP	R1, R2
	BNE	_ramZ

	/*
	 * turn SMP on
	 * invalidate tlb
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ORR	$CpACsmp, R1		/* turn SMP on */
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BARRIERS
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/*
	 * start stack at top of mach (physical addr)
	 * set up page tables for kernel
	 */
	MOVW	$PADDR(MACHADDR+MACHSIZE-4), R13
	MOVW	$PADDR(L1), R0
	BL	mmuinit(SB)

	/*
	 * set up domain access control and page table base
	 */
	MOVW	$Client, R1
	MCR	CpSC, 0, R1, C(CpDAC), C(0)
	MOVW	$PADDR(L1), R1
	ORR	$(CpTTBs|CpTTBowba|CpTTBiwba), R1
	MCR	CpSC, 0, R1, C(CpTTB), C(0)
	MCR	CpSC, 0, R1, C(CpTTB), C(0), CpTTB1	/* cortex has two */

	/*
	 * invalidate my caches before enabling
	 */
	BL	cachedinv(SB)
	BL	cacheiinv(SB)
	BL	l2cacheuinv(SB)
	BARRIERS

	/*
	 * enable caches, mmu, and high vectors
	 */

	MRC	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpChv|CpCdcache|CpCicache|CpCmmu), R0
	MCR	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/*
	 * switch SB, SP, and PC into KZERO space
	 */
	MOVW	$setR12(SB), R12
	MOVW	$(MACHADDR+MACHSIZE-4), R13
	MOVW	$_startpg(SB), R15

TEXT _startpg(SB), 1, $-4

	/*
	 * enable cycle counter
	 */
	MOVW	$(1<<31), R1
	MCR	CpSC, 0, R1, C(CpCLD), C(CpCLDena), CpCLDenacyc
	MOVW	$1, R1
	MCR	CpSC, 0, R1, C(CpCLD), C(CpCLDena), CpCLDenapmnc

	/*
	 * call main and loop forever if it returns
	 */
	BL	,main(SB)
	B	,0(PC)

	BL	_div(SB)		/* hack to load _div, etc. */

/*
 * startup entry for cpu(s) other than 0
 */
TEXT cpureset(SB), 1, $-4
reset:
	/*
	 * load physical base for SB addressing while mmu is off
	 * keep a handy zero in R0 until first function call
	 */
	MOVW	$setR12(SB), R12
	SUB	$KZERO, R12
	ADD	$PHYSDRAM, R12
	MOVW	$0, R0

	/*
	 * SVC mode, interrupts disabled
	 */
	BL	svcmode(SB)

	/*
	 * disable the mmu and caches
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCmmu), R1
	ORR	$(CpCsbo|CpCsw), R1
	BIC	$CpCsbz, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/*
	 * turn SMP on
	 * invalidate tlb
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ORR	$CpACsmp, R1		/* turn SMP on */
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BARRIERS
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/*
	 * find Mach for this cpu
	 */
	MRC	CpSC, 0, R2, C(CpID), C(CpIDidct), CpIDmpid
	AND	$(MAXMACH-1), R2	/* mask out non-cpu-id bits */
	SLL	$2, R2			/* convert to word index */
	MOVW	$machaddr(SB), R0
	BIC		$KSEGM, R0
	ADD	R2, R0			/* R0 = &machaddr[cpuid] */
	MOVW	(R0), R0		/* R0 = machaddr[cpuid] */
	CMP	$0, R0
	BEQ	0(PC)			/* must not be zero */
	SUB	$KZERO, R0, R(MACH)	/* m = PADDR(machaddr[cpuid]) */

	/*
	 * start stack at top of local Mach
	 */
	ADD	$(MACHSIZE-4), R(MACH), R13

	/*
	 * set up domain access control and page table base
	 */
	MOVW	$Client, R1
	MCR	CpSC, 0, R1, C(CpDAC), C(0)
	MOVW	12(R(MACH)), R1	/* m->mmul1 */
	SUB	$KZERO, R1		/* phys addr */
	ORR	$(CpTTBs|CpTTBowba|CpTTBiwba), R1
	MCR	CpSC, 0, R1, C(CpTTB), C(0)
	MCR	CpSC, 0, R1, C(CpTTB), C(0), CpTTB1	/* cortex has two */

	/*
	 * invalidate my caches before enabling
	 */
	BL	cachedinv(SB)
	BL	cacheiinv(SB)
	BARRIERS

	/*
	 * enable caches, mmu, and high vectors
	 */
	MRC	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpChv|CpCdcache|CpCicache|CpCmmu), R0
	MCR	CpSC, 0, R0, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/*
	 * switch MACH, SB, SP, and PC into KZERO space
	 */
	ADD	$KZERO, R(MACH)
	MOVW	$setR12(SB), R12
	ADD	$KZERO, R13
	MOVW	$_startpg2(SB), R15

TEXT _startpg2(SB), 1, $-4

	/*
	 * enable cycle counter
	 */
	MOVW	$(1<<31), R1
	MCR	CpSC, 0, R1, C(CpCLD), C(CpCLDena), CpCLDenacyc
	MOVW	$1, R1
	MCR	CpSC, 0, R1, C(CpCLD), C(CpCLDena), CpCLDenapmnc

	/*
	 * call cpustart and loop forever if it returns
	 */
	MRC	CpSC, 0, R0, C(CpID), C(CpIDidct), CpIDmpid
	AND	$(MAXMACH-1), R0		/* mask out non-cpu-id bits */
	BL	,cpustart(SB)
	B	,0(PC)

/*
 * get into SVC mode with interrupts disabled
 * raspberry pi firmware since 29 Sept 2015 starts in HYP mode
 */
TEXT svcmode(SB), 1, $-4
	MOVW	CPSR, R1
	AND	$PsrMask, R1
	MOVW	$PsrMhyp, R2
	CMP	R2, R1
	MOVW	$(PsrDirq|PsrDfiq|PsrMsvc), R1
	BNE	nothyp
	MSR(1, 1, 1, 0xe)	/* MOVW	R1, SPSR_HYP */
	MSR(0, 14, 1, 0xe)	/* MOVW	R14, ELR_HYP */
	ERET
nothyp:
	MOVW	R1, CPSR
	RET

TEXT cpidget(SB), 1, $-4			/* main ID */
	MRC	CpSC, 0, R0, C(CpID), C(0), CpIDid
	RET

TEXT fsrget(SB), 1, $-4				/* data fault status */
	MRC	CpSC, 0, R0, C(CpFSR), C(0), CpFSRdata
	RET

TEXT ifsrget(SB), 1, $-4			/* instruction fault status */
	MRC	CpSC, 0, R0, C(CpFSR), C(0), CpFSRinst
	RET

TEXT farget(SB), 1, $-4				/* fault address */
	MRC	CpSC, 0, R0, C(CpFAR), C(0x0)
	RET

TEXT cpctget(SB), 1, $-4			/* cache type */
	MRC	CpSC, 0, R0, C(CpID), C(CpIDidct), CpIDct
	RET

TEXT lcycles(SB), 1, $-4
	MRC	CpSC, 0, R0, C(CpCLD), C(CpCLDcyc), 0
	RET

TEXT splhi(SB), 1, $-4
	MOVW	R14, 4(R(MACH))		/* save caller pc in m->splpc */

	MOVW	CPSR, R0			/* turn off irqs (but not fiqs) */
	ORR	$(PsrDirq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splfhi(SB), 1, $-4
	MOVW	R14, 4(R(MACH))		/* save caller pc in m->splpc */

	MOVW	CPSR, R0			/* turn off irqs and fiqs */
	ORR	$(PsrDirq|PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splflo(SB), 1, $-4
	MOVW	CPSR, R0			/* turn on fiqs */
	BIC	$(PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT spllo(SB), 1, $-4
	MOVW	CPSR, R0			/* turn on irqs and fiqs */
	MOVW	$0, R1
	CMP.S	R1, R(MACH)
	MOVW.NE	R1, 4(R(MACH))			/* clear m->splpc */
	BIC	$(PsrDirq|PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splx(SB), 1, $-4
	MOVW	R14, 4(R(MACH))		/* save caller pc in m->splpc */

	MOVW	R0, R1				/* reset interrupt level */
	MOVW	CPSR, R0
	MOVW	R1, CPSR
	RET

TEXT spldone(SB), 1, $0				/* end marker for devkprof.c */
	RET

TEXT islo(SB), 1, $-4
	MOVW	CPSR, R0
	AND	$(PsrDirq), R0
	EOR	$(PsrDirq), R0
	RET

TEXT	tas(SB), $-4
TEXT	_tas(SB), $-4			/* _tas(ulong *) */
	/* returns old (R0) after modifying (R0) */
	MOVW	R0,R5
	DMB

	MOVW	$1,R2		/* new value of (R0) */
	MOVW	$MAXSC, R8
tas1:
	LDREX(5,7)		/* LDREX 0(R5),R7 */
	CMP.S	$0, R7		/* old value non-zero (lock taken)? */
	BNE	lockbusy	/* we lose */
	SUB.S	$1, R8
	BEQ	lockloop2
	STREX(2,5,4)		/* STREX R2,(R5),R4 */
	CMP.S	$0, R4
	BNE	tas1		/* strex failed? try again */
	DMB
	B	tas0
lockloop2:
	BL	abort(SB)
lockbusy:
	CLREX
tas0:
	MOVW	R7, R0		/* return old value */
	RET

TEXT setlabel(SB), 1, $-4
	MOVW	R13, 0(R0)		/* sp */
	MOVW	R14, 4(R0)		/* pc */
	MOVW	$0, R0
	RET

TEXT gotolabel(SB), 1, $-4
	MOVW	0(R0), R13		/* sp */
	MOVW	4(R0), R14		/* pc */
	MOVW	$1, R0
	RET

TEXT getcallerpc(SB), 1, $-4
	MOVW	0(R13), R0
	RET

TEXT idlehands(SB), $-4
	MOVW	CPSR, R3
	ORR	$(PsrDirq|PsrDfiq), R3, R1		/* splfhi */
	MOVW	R1, CPSR

	DSB
	MOVW	nrdy(SB), R0
	CMP	$0, R0
	WFI_EQ
	DSB

	MOVW	R3, CPSR			/* splx */
	RET


TEXT coherence(SB), $-4
	BARRIERS
	RET

TEXT sev(SB), $-4
	SEV
	RET

/*
 * invalidate tlb
 */
TEXT mmuinvalidate(SB), 1, $-4
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS
	RET

/*
 * mmuinvalidateaddr(va)
 *   invalidate tlb entry for virtual page address va, ASID 0
 */
TEXT mmuinvalidateaddr(SB), 1, $-4
	DSB
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinvse
	BARRIERS
	RET

/*
 * `single-element' cache operations.
 * in arm arch v7, if effective to PoC, they operate on all cache levels, so separate
 * l2 functions are unnecessary.
 */

TEXT cachedwbse(SB), $-4			/* D writeback SE */
	MOVW	R0, R2

	MOVW	CPSR, R3
	CPSID					/* splhi */

	BARRIERS			/* force outstanding stores to cache */
	MOVW	R2, R0
	MOVW	4(FP), R1
	ADD	R0, R1				/* R1 is end address */
	BIC	$(CACHELINESZ-1), R0		/* cache line start */
_dwbse:
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEse
	/* can't have a BARRIER here since it zeroes R0 */
	ADD	$CACHELINESZ, R0
	CMP.S	R0, R1
	BGT	_dwbse
	B	_wait

/*
 * TLB on armv7 loads from cache, so no need for writeback
 */
TEXT cachedwbtlb(SB), $-4
	DSB
	ISB
	RET

TEXT cachedwbinvse(SB), $-4			/* D writeback+invalidate SE */
	MOVW	R0, R2

	MOVW	CPSR, R3
	CPSID					/* splhi */

	BARRIERS			/* force outstanding stores to cache */
	MOVW	R2, R0
	MOVW	4(FP), R1
	ADD	R0, R1				/* R1 is end address */
	BIC	$(CACHELINESZ-1), R0		/* cache line start */
_dwbinvse:
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwbi), CpCACHEse
	/* can't have a BARRIER here since it zeroes R0 */
	ADD	$CACHELINESZ, R0
	CMP.S	R0, R1
	BGT	_dwbinvse
_wait:						/* drain write buffer */
	BARRIERS

	MOVW	R3, CPSR			/* splx */
	RET

TEXT cachedinvse(SB), $-4			/* D invalidate SE */
	MOVW	R0, R2

	MOVW	CPSR, R3
	CPSID					/* splhi */

	BARRIERS			/* force outstanding stores to cache */
	MOVW	R2, R0
	MOVW	4(FP), R1
	ADD	R0, R1				/* R1 is end address */
	BIC	$(CACHELINESZ-1), R0		/* cache line start */
_dinvse:
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvd), CpCACHEse
	/* can't have a BARRIER here since it zeroes R0 */
	ADD	$CACHELINESZ, R0
	CMP.S	R0, R1
	BGT	_dinvse
	B	_wait

#include "cache.v7.s"
boot.rc

#!/boot/rc -m /boot/rcmain

/boot/echo -n boot...
path=(/bin /boot)
bind '#p' /proc
bind '#d' /fd
bind -a '#P' /dev
bind -a '#t' /dev
bind -a '#S' /dev
bind -a '#I' /net
echo -n rpi >/dev/hostowner
echo -n fdisk...
fdisk -p /dev/sdM0/data >/dev/sdM0/ctl
dossrv -f/dev/sdM0/dos boot
rootdir=/root/plan9
rootspec=''
mount -c /srv/boot /root
bind -ac $rootdir /
bind -ac $rootdir/mnt /mnt

bind /$cputype/bin /bin
bind -a /rc/bin /bin

if (! ~ $#init 0)
	exec `{echo $init}
if (~ $service cpu)
	exec /$cputype/init -c
if not
	exec /$cputype/init -t
exec /boot/rc -m/boot/rcmain -i
ERS

	/*
	 * clear mach and page tables
	 */
	MOVW	$PADDR(MACHADDR), R1
	MOVW	$PADDR(KTZERO), R2
_ramZ:
	MOVW	R0, (R1)
	ADD	$4, R1
	CMP	R1, R2
	BNE	_ramZ

	/*
	 * turn SMP on
	 * invalidate tlb
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ORR	$CpACsmp, R1		/* turn SMP on */
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BARRIERS
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/*
	 * start stack at top of mach (physicalbootwifi.rc
     * #!/boot/rc -m /boot/rcmain

wifi=/net/ether1

fn need {
	while (~ $#$1 0) {
		echo -n $1': '
		$1=`{read}
	}
}

fn joinwifi {
	need essid
	echo essid $essid >$wifi/clone
	need wificrypt
	wifip=p
	if (! ~ $#wifipass 0) {
		factotum -g 'proto=wpapsk essid='$essid' !password='$wifipass
		wifip=''
	}
	switch ($wificrypt) {
		case wep
			echo crypt wep >$wifi/clone
			need wep_password
			echo key0 $wep_password >$wifi/clone
			rm /env/wep_password
		case wpa wpa1
			wpa -1p $wifi
		case wpa2
			wpa -2$wifip $wifi
	}
}

path=(/bin /boot)
bind '#p' /proc
bind '#d' /fd
bind -a '#P' /dev
bind -a '#t' /dev
bind -a '#S' /dev
bind -a '#I' /net
bind -a '#l0' /net
bind -a '#l1' /net || wifi=/net/ether0

usbd

need fs
need auth

factotum -u -s factotum -a $auth
joinwifi
ipconfig ether $wifi
srv tcp!$fs!564 boot

rootdir=/root
rootspec=''
mount -c /srv/boot /root
bind -ac $rootdir /
bind -ac $rootdir/mnt /mnt

bind /$cputype/bin /bin
bind -a /rc/bin /bin
path=(. /bin)

if (! ~ $#init 0)
	exec `{echo $init}
if (~ $service cpu)
	exec /$cputype/init -c
if not
	exec /$cputype/init -t
exec /boot/rc -m/boot/rcmain -i
mmu), R1
	ORR	$(CpCsbo|CpCsw), R1
	BIC	$CpCsbz, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

	/*
	 * turn SMP on
	 * invalidate tlb
	 */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ORR	$CpACsmp, R1		/* turn SMP on */
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BARRIERS
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/*
	 * find Mach for this cpu
	 */
	MRC	CpSC, 0, R2, C(CpID), C(cache.v7.s
    
    /*
 * cortex arm arch v7 cache flushing and invalidation
 * shared by l.s and rebootcode.s
 */

#define	BPIALL	MCR CpSC, 0, R0, C(CpCACHE), C(5), 6	/* branch predictor invalidate all */

TEXT cacheiinv(SB), $-4				/* I invalidate */
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall /* ok on cortex */
	BPIALL	/* redundant? */
	DSB
	ISB
	RET

TEXT cacheiinvse(SB), $0			/* I invalidate SE */
	MOVW 4(FP), R1
	ADD	R0, R1
	BIC $(ICACHELINESZ - 1), R0
	DSB
_iinvse:
	MCR CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEse
	ADD $ICACHELINESZ, R0
	CMP.S R0, R1
	BGT _iinvse
	BPIALL
	DSB
	ISB
	RET

/*
 * set/way operators, passed a suitable set/way value in R0.
 */
TEXT cachedwb_sw(SB), $-4
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEsi
	RET

TEXT cachedwbinv_sw(SB), $-4
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwbi), CpCACHEsi
	RET

TEXT cachedinv_sw(SB), $-4
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvd), CpCACHEsi
	RET

	/* set cache size select */
TEXT setcachelvl(SB), $-4
	MCR	CpSC, CpIDcssel, R0, C(CpID), C(CpIDidct), 0
	ISB
	RET

	/* return cache sizes */
TEXT getwayssets(SB), $-4
	MRC	CpSC, CpIDcsize, R0, C(CpID), C(CpIDidct), 0
	RET

/*
 * l1 cache operations.
 * l1 and l2 ops are intended to be called from C, thus need save no
 * caller's regs, only those we need to preserve across calls.
 */

TEXT cachedwb(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	$cachedwb_sw(SB), R0
	MOVW	$1, R8
	BL	wholecache(SB)
	MOVW.P	8(R13), R15

TEXT cachedwbinv(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	$cachedwbinv_sw(SB), R0
	MOVW	$1, R8
	BL	wholecache(SB)
	MOVW.P	8(R13), R15

TEXT cachedinv(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	$cachedinv_sw(SB), R0
	MOVW	$1, R8
	BL	wholecache(SB)
	MOVW.P	8(R13), R15

TEXT cacheuwbinv(SB), $-4
	MOVM.DB.W [R14], (R13)	/* save lr on stack */
	MOVW	CPSR, R1
	CPSID			/* splhi */

	MOVM.DB.W [R1], (R13)	/* save R1 on stack */

	BL	cachedwbinv(SB)
	BL	cacheiinv(SB)

	MOVM.IA.W (R13), [R1]	/* restore R1 (saved CPSR) */
	MOVW	R1, CPSR
	MOVM.IA.W (R13), [R14]	/* restore lr */
	RET

/*
 * l2 cache operations
 */

TEXT l2cacheuwb(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	$cachedwb_sw(SB), R0
	MOVW	$2, R8
	BL	wholecache(SB)
	MOVW.P	8(R13), R15

TEXT l2cacheuwbinv(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	CPSR, R1
	CPSID			/* splhi */

	MOVM.DB.W [R1], (R13)	/* save R1 on stack */

	MOVW	$cachedwbinv_sw(SB), R0
	MOVW	$2, R8
	BL	wholecache(SB)
	BL	l2cacheuinv(SB)

	MOVM.IA.W (R13), [R1]	/* restore R1 (saved CPSR) */
	MOVW	R1, CPSR
	MOVW.P	8(R13), R15

TEXT l2cacheuinv(SB), $-4
	MOVW.W	R14, -8(R13)
	MOVW	$cachedinv_sw(SB), R0
	MOVW	$2, R8
	BL	wholecache(SB)
	MOVW.P	8(R13), R15

/*
 * these shift values are for the Cortex-A8 L1 cache (A=2, L=6) and
 * the Cortex-A8 L2 cache (A=3, L=6).
 * A = log2(# of ways), L = log2(bytes per cache line).
 * see armv7 arch ref p. 1403.
 */
#define L1WAYSH 30
#define L1SETSH 6
#define L2WAYSH 29
#define L2SETSH 6

/*
 * callers are assumed to be the above l1 and l2 ops.
 * R0 is the function to call in the innermost loop.
 * R8 is the cache level (one-origin: 1 or 2).
 *
 * initial translation by 5c, then massaged by hand.
 */
TEXT wholecache+0(SB), $-4
	MOVW	R0, R1		/* save argument for inner loop in R1 */
	SUB	$1, R8		/* convert cache level to zero origin */

	/* we may not have the MMU on yet, so map R1 to PC's space */
	BIC	$KSEGM,	R1	/* strip segment from address */
	MOVW	PC, R2		/* get PC's segment ... */
	AND	$KSEGM, R2
	ORR	R2, R1		/* combine them */

	/* drain write buffers */
	BARRIERS
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEwait
	ISB

	MOVW	CPSR, R2
	MOVM.DB.W [R2,R14], (SP) /* save regs on stack */
	CPSID			/* splhi to make entire op atomic */

	/* get cache sizes */
	SLL	$1, R8, R0	/* R0 = (cache - 1) << 1 */
	MCR	CpSC, CpIDcssel, R0, C(CpID), C(CpIDidct), 0 /* set cache size select */
	ISB
	MRC	CpSC, CpIDcsize, R0, C(CpID), C(CpIDidct), 0 /* get cache sizes */

	/* compute # of ways and sets for this cache level */
	SRA	$3, R0, R5	/* R5 (ways) = R0 >> 3 */
	AND	$1023, R5	/* R5 = (R0 >> 3) & MASK(10) */
	ADD	$1, R5		/* R5 (ways) = ((R0 >> 3) & MASK(10)) + 1 */

	SRA	$13, R0, R2	/* R2 = R0 >> 13 */
	AND	$32767, R2	/* R2 = (R0 >> 13) & MASK(15) */
	ADD	$1, R2		/* R2 (sets) = ((R0 >> 13) & MASK(15)) + 1 */

	/* precompute set/way shifts for inner loop */
	CMP	$0, R8		/* cache == 1? */
	MOVW.EQ	$L1WAYSH, R3 	/* yes */
	MOVW.EQ	$L1SETSH, R4
	MOVW.NE	$L2WAYSH, R3	/* no */
	MOVW.NE	$L2SETSH, R4

	/* iterate over ways */
	MOVW	$0, R7		/* R7: way */
outer:
	/* iterate over sets */
	MOVW	$0, R6		/* R6: set */
inner:
	/* compute set/way register contents */
	SLL	R3, R7, R0 	/* R0 = way << R3 (L?WAYSH) */
	ORR	R8<<1, R0	/* R0 = way << L?WAYSH | (cache - 1) << 1 */
	ORR	R6<<R4, R0 	/* R0 = way<<L?WAYSH | (cache-1)<<1 |set<<R4 */

	BL	(R1)		/* call set/way operation with R0 */

	ADD	$1, R6		/* set++ */
	CMP	R2, R6		/* set >= sets? */
	BLT	inner		/* no, do next set */

	ADD	$1, R7		/* way++ */
	CMP	R5, R7		/* way >= ways? */
	BLT	outer		/* no, do next way */

	MOVM.IA.W (SP), [R2,R14] /* restore regs */
	MOVW	R2, CPSR	/* splx */

	/* drain write buffers */
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEwait
	ISB
	RET
SID					/* splhi */

	BARRIERS			/* force outstanding stores to cache */
	MOVW	R2, R0
	MOVW	4(FP), R1
	ADD	R0, R1				/* R1 is end address */
	BIC	$(CACHELINESZ-1), R0		/* cache line start */
_dwbinvse:
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwbi), CpCACHEse
	/* can't have a BARRIER here since it zeroes R0 */
	ADD	$CACHELINESZ, R0
	CMP.S	R0, R1
	BGT	_dwbinvse
_wait:						/* drain write buffer */
	BARRIERS

	MOVW	R3, CPSR			/* splx */
	RET

TEXT cachedinvse(SB), $-4			/* D inclock.c/*
 * bcm283[56] timers
 *	System timers run at 1MHz (timers 1 and 2 are used by GPU)
 *	ARM timer usually runs at 250MHz (may be slower in low power modes)
 *	Cycle counter runs at 700MHz (unless overclocked)
 *    All are free-running up-counters
 *  Cortex-a7 has local generic timers per cpu (which we run at 1MHz)
 *
 * Use system timer 3 (64 bits) for hzclock interrupts and fastticks
 *   For smp on bcm2836, use local generic timer for interrupts on cpu1-3
 * Use ARM timer (32 bits) for perfticks
 * Use ARM timer to force immediate interrupt
 * Use cycle counter for cycles()
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "arm.h"

enum {
	SYSTIMERS	= VIRTIO+0x3000,
	ARMTIMER	= VIRTIO+0xB400,

	Localctl	= 0x00,
	Prescaler	= 0x08,
	Localintpending	= 0x60,

	SystimerFreq	= 1*Mhz,
	MaxPeriod	= SystimerFreq / HZ,
	MinPeriod	= 10,

};

typedef struct Systimers Systimers;
typedef struct Armtimer Armtimer;

struct Systimers {
	u32int	cs;
	u32int	clo;
	u32int	chi;
	u32int	c0;
	u32int	c1;
	u32int	c2;
	u32int	c3;
};

struct Armtimer {
	u32int	load;
	u32int	val;
	u32int	ctl;
	u32int	irqack;
	u32int	irq;
	u32int	maskedirq;
	u32int	reload;
	u32int	predivider;
	u32int	count;
};

enum {
	CntPrescaleShift= 16,	/* freq is sys_clk/(prescale+1) */
	CntPrescaleMask	= 0xFF,
	CntEnable	= 1<<9,
	TmrDbgHalt	= 1<<8,
	TmrEnable	= 1<<7,
	TmrIntEnable	= 1<<5,
	TmrPrescale1	= 0x00<<2,
	TmrPrescale16	= 0x01<<2,
	TmrPrescale256	= 0x02<<2,
	CntWidth16	= 0<<1,
	CntWidth32	= 1<<1,

	/* generic timer (cortex-a7) */
	Enable	= 1<<0,
	Imask	= 1<<1,
	Istatus = 1<<2,
};

static void
clockintr(Ureg *ureg, void *)
{
	Systimers *tn;

	if(m->machno != 0)
		panic("cpu%d: unexpected system timer interrupt", m->machno);
	tn = (Systimers*)SYSTIMERS;
	/* dismiss interrupt */
	tn->c3 = tn->clo - 1;
	tn->cs = 1<<3;
	timerintr(ureg, 0);
}

static void
localclockintr(Ureg *ureg, void *)
{
	if(m->machno == 0)
		panic("cpu0: Unexpected local generic timer interrupt");
	cpwrsc(0, CpTIMER, CpTIMERphys, CpTIMERphysctl, Imask|Enable);
	timerintr(ureg, 0);
}

void
clockshutdown(void)
{
	Armtimer *tm;

	tm = (Armtimer*)ARMTIMER;
	tm->ctl = 0;
	if(cpuserver)
		wdogfeed();
	else
		wdogoff();
}

void
clockinit(void)
{
	Systimers *tn;
	Armtimer *tm;
	u32int t0, t1, tstart, tend;

	if(((cprdsc(0, CpID, CpIDfeat, 1) >> 16) & 0xF) != 0) {
		/* generic timer supported */
		if(m->machno == 0){
			/* input clock is 19.2MHz or 54MHz crystal */
			*(ulong*)(ARMLOCAL + Localctl) = 0;
			/* divide by (2^31/Prescaler) for 1Mhz */
			*(ulong*)(ARMLOCAL + Prescaler) = (((uvlong)SystimerFreq<<31)/soc.oscfreq)&~1UL;
		}
		cpwrsc(0, CpTIMER, CpTIMERphys, CpTIMERphysctl, Imask);
	}

	tn = (Systimers*)SYSTIMERS;
	tstart = tn->clo;
	do{
		t0 = lcycles();
	}while(tn->clo == tstart);
	tend = tstart + 10000;
	do{
		t1 = lcycles();
	}while(tn->clo != tend);
	t1 -= t0;
	m->cpuhz = 100 * t1;
	m->cpumhz = (m->cpuhz + Mhz/2 - 1) / Mhz;
	m->cyclefreq = m->cpuhz;
	if(m->machno == 0){
		tn->c3 = tn->clo - 1;
		tm = (Armtimer*)ARMTIMER;
		tm->load = 0;
		tm->ctl = TmrPrescale1|CntEnable|CntWidth32;
		intrenable(IRQtimer3, clockintr, nil, 0, "clock");
	}else
		intrenable(IRQcntpns, localclockintr, nil, 0, "clock");
}

void
timerset(uvlong next)
{
	Systimers *tn;
	uvlong now;
	long period;

	now = fastticks(nil);
	period = next - now;
	if(period < MinPeriod)
		period = MinPeriod;
	else if(period > MaxPeriod)
		period = MaxPeriod;
	if(m->machno > 0){
		cpwrsc(0, CpTIMER, CpTIMERphys, CpTIMERphysval, period);
		cpwrsc(0, CpTIMER, CpTIMERphys, CpTIMERphysctl, Enable);
	}else{
		tn = (Systimers*)SYSTIMERS;
		tn->c3 = tn->clo + period;
	}
}

uvlong
fastticks(uvlong *hz)
{
	Systimers *tn;
	ulong lo, hi;
	uvlong now;

	if(hz)
		*hz = SystimerFreq;
	tn = (Systimers*)SYSTIMERS;
	do{
		hi = tn->chi;
		lo = tn->clo;
	}while(tn->chi != hi);
	now = (uvlong)hi<<32 | lo;
	return now;
}

ulong
perfticks(void)
{
	Armtimer *tm;

	tm = (Armtimer*)ARMTIMER;
	return tm->count;
}

void
armtimerset(int n)
{
	Armtimer *tm;

	tm = (Armtimer*)ARMTIMER;
	if(n > 0){
		tm->ctl |= TmrEnable|TmrIntEnable;
		tm->load = n;
	}else{
		tm->load = 0;
		tm->ctl &= ~(TmrEnable|TmrIntEnable);
		tm->irq = 1;
	}
}

ulong
µs(void)
{
	if(SystimerFreq != 1*Mhz)
		return fastticks2us(fastticks(nil));
	return ((Systimers*)SYSTIMERS)->clo;
}

void
microdelay(int n)
{
	Systimers *tn;
	u32int now, diff;

	diff = n + 1;
	tn = (Systimers*)SYSTIMERS;
	now = tn->clo;
	while(tn->clo - now < diff)
		;
}

void
delay(int n)
{
	while(--n >= 0)
		microdelay(1000);
}
pSC, 0, R0, C(CpCAcoproc.c

#include "../teg2/coproc.c"
/*
 * Time.
 *
 * HZ should divide 1000 evenly, ideally.
 * 100, 125, 200, 250 and 333 are okay.
 */
#define	HZ		100			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

enum {
	Mhz	= 1000 * 1000,
};

typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct I2Cdev	I2Cdev;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct Memcache	Memcache;
typedef struct MMMU	MMMU;
typedef struct Mach	Mach;
typedef struct Notsave	Notsave;
typedef struct Page	Page;
typedef struct PhysUart	PhysUart;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef u32int		PTE;
typedef struct Soc	Soc;
typedef struct Uart	Uart;
typedef struct Ureg	Ureg;
typedef uvlong		Tval;

#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(E_MAGIC)

struct Lock
{
	ulong	key;
	u32int	sr;
	uintptr	pc;
	Proc*	p;
	Mach*	m;
	int	isilock;
};

struct Label
{
	uintptr	sp;
	uintptr	pc;
};

enum {
	Maxfpregs	= 32,	/* could be 16 or 32, see Mach.fpnregs */
	Nfpctlregs	= 16,
};

/*
 * emulated or vfp3 floating point
 */
struct FPsave
{
	ulong	status;
	ulong	control;
	/*
	 * vfp3 with ieee fp regs; uvlong is sufficient for hardware but
	 * each must be able to hold an Internal from fpi.h for sw emulation.
	 */
	ulong	regs[Maxfpregs][3];

	int	fpstate;
	uintptr	pc;		/* of failed fp instr. */
};

/*
 * FPsave.fpstate
 */
enum
{
	FPinit,
	FPactive,
	FPinactive,
	FPemu,

	/* bits or'd with the state */
	FPillegal= 0x100,
};

struct Confmem
{
	uintptr	base;
	usize	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[2];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	usize	upages;		/* user page pool */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	ulong	hz;		/* processor cycle freq */
	ulong	mhz;
	int	monitor;	/* flag */
};

struct I2Cdev {
	int	salen;
	int	addr;
	int	tenbit;
};

/*
 * GPIO
 */
enum {
	Input	= 0x0,
	Output	= 0x1,
	Alt0	= 0x4,
	Alt1	= 0x5,
	Alt2	= 0x6,
	Alt3	= 0x7,
	Alt4	= 0x3,
	Alt5	= 0x2,
};



/*
 *  things saved in the Proc structure during a notify
 */
struct Notsave {
	int	emptiness;
};

/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	PTE*	mmul1;		/* l1 for this processor */
	int	mmul1lo;
	int	mmul1hi;
	int	mmupid;
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1		/* 1 level cache, don't worry about VCE's */
struct PMMU
{
	Page*	mmul2;
	Page*	mmul2cache;	/* free mmu pages */
};

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */

	Proc*	proc;			/* current process */

	MMMU;
	int	flushmmu;		/* flush current proc mmu state */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	cputype;
	ulong	delayloop;

	/* stats */
	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	uvlong	fastclock;		/* last sampled value */
	ulong	spuriousintr;
	int	lastintr;
	int	ilockdepth;
	Perf	perf;			/* performance counters */


	int	cpumhz;
	uvlong	cpuhz;			/* speed of cpu */
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */

	/* vfp2 or vfp3 fpu */
	int	havefp;
	int	havefpvalid;
	int	fpon;
	int	fpconfiged;
	int	fpnregs;
	ulong	fpscr;			/* sw copy */
	int	fppid;			/* pid of last fault */
	uintptr	fppc;			/* addr of last fault */
	int	fpcnt;			/* how many consecutive at that addr */

	/* save areas for exceptions, hold R0-R4 */
	u32int	sfiq[5];
	u32int	sirq[5];
	u32int	sund[5];
	u32int	sabt[5];
	u32int	smon[5];		/* probably not needed */
	u32int	ssys[5];

	int	stack[1];
};

/*
 * Fake kmap.
 */
typedef void		KMap;
#define	VA(k)		((uintptr)(k))
#define	kmap(p)		(KMap*)((p)->pa|kseg0)
extern void kunmap(KMap*);

struct
{
	Lock;
	int	machs;			/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
}active;

extern register Mach* m;			/* R10 */
extern register Proc* up;			/* R9 */
extern uintptr kseg0;
extern Mach* machaddr[MAXMACH];
extern ulong memsize;
extern int normalprint;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char	*type;
	ulong	port;
	int	irq;
	ulong	dma;
	ulong	mem;
	ulong	size;
	ulong	freq;

	int	nopt;
	char	*opt[NISAOPT];
};

#define	MACHP(n)	(machaddr[n])

/*
 * Horrid. But the alternative is 'defined'.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

int vflag;
extern char dbgflg[256];

#define dbgprint	print		/* for now */

/*
 *  hardware info about a device
 */
typedef struct {
	ulong	port;
	int	size;
} Devport;

struct DevConf
{
	ulong	intnum;			/* interrupt number */
	char	*type;			/* card type, malloced */
	int	nports;			/* Number of ports */
	Devport	*ports;			/* The ports themselves */
};

struct Soc {			/* SoC dependent configuration */
	ulong	dramsize;
	uintptr	physio;
	uintptr	busdram;
	uintptr	busio;
	uintptr	armlocal;
	uint	oscfreq;
	u32int	l1ptedramattrs;
	u32int	l2ptedramattrs;
};
extern Soc soc;
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

enum {
	Qdir = 0,
	Qbase,

	Qmax = 16,
};

typedef long Rdwrfn(Chan*, void*, long, vlong);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
};

Lock archwlock;	/* the lock is only for changing archdir */
int narchdir = Qbase;

/*
 * Add a file to the #P listing.  Once added, you can't delete it.
 * You can't add a file with the same name as one already there,
 * and you get a pointer to the Dirtab entry so you can do things
 * like change the Qid version.  Changing the Qid path is disallowed.
 */
Dirtab*
addarchfile(char *name, int perm, Rdwrfn *rdfn, Rdwrfn *wrfn)
{
	int i;
	Dirtab d;
	Dirtab *dp;

	memset(&d, 0, sizeof d);
	strcpy(d.name, name);
	d.perm = perm;

	lock(&archwlock);
	if(narchdir >= Qmax){
		unlock(&archwlock);
		return nil;
	}

	for(i=0; i<narchdir; i++)
		if(strcmp(archdir[i].name, name) == 0){
			unlock(&archwlock);
			return nil;
		}

	d.qid.path = narchdir;
	archdir[narchdir] = d;
	readfn[narchdir] = rdfn;
	writefn[narchdir] = wrfn;
	dp = &archdir[narchdir++];
	unlock(&archwlock);

	return dp;
}

static Chan*
archattach(char* spec)
{
	return devattach('P', spec);
}

Walkqid*
archwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int
archstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static Chan*
archopen(Chan* c, int omode)
{
	return devopen(c, omode, archdir, narchdir, devgen);
}

static void
archclose(Chan*)
{
}

static long
archread(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);

	default:
		if(c->qid.path < narchdir && (fn = readfn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
		break;
	}

	return 0;
}

static long
archwrite(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	if(c->qid.path < narchdir && (fn = writefn[c->qid.path]))
		return fn(c, a, n, offset);
	error(Eperm);

	return 0;
}

void archinit(void);

Dev archdevtab = {
	'P',
	"arch",

	devreset,
	archinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

static long
cputyperead(Chan*, void *a, long n, vlong offset)
{
	char name[64], str[128];

	cputype2name(name, sizeof name);
	snprint(str, sizeof str, "ARM %s %d\n", name, m->cpumhz);
	return readstr(offset, a, n, str);
}

static long
cputempread(Chan*, void *a, long n, vlong offset)
{
	char str[16];

	snprint(str, sizeof str, "%ud\n", (getcputemp()+500)/1000);
	return readstr(offset, a, n, str);
}

void
archinit(void)
{
	addarchfile("cputype", 0444, cputyperead, nil);
	addarchfile("cputemp", 0444, cputempread, nil);
}
devether.c#include "../omap/devether.c"
/*
 * raspberry pi doesn't have a realtime clock
 * fake a crude approximation from the kernel build time
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum{
	Qdir = 0,
	Qrtc,
};

Dirtab rtcdir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"rtc",		{Qrtc, 0},	0,	0664,
};

extern ulong kerndate;

static ulong rtcsecs;

static void
rtctick(void)
{
	rtcsecs++;
}

static void
rtcinit(void)
{
	rtcsecs = kerndate;
	addclock0link(rtctick, 1000);
}

static long
rtcread(Chan *c, void *a, long n, vlong offset)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	case Qrtc:
		return readnum((ulong)offset, a, n, rtcsecs, 12);
	}
	error(Ebadarg);
	return 0;
}

static long
rtcwrite(Chan*c, void *a, long n, vlong)
{
	char b[13];
	ulong i;

	switch((ulong)c->qid.path){
	case Qrtc:
		if(n >= sizeof(b))
			error(Ebadarg);
		strncpy(b, (char*)a, n);
		i = strtol(b, 0, 0);
		if(i <= 0)
			error(Ebadarg);
		rtcsecs = i;
		return n;
	}
	error(Eperm);
	return 0;
}

static Chan*
rtcattach(char* spec)
{
	return devattach('r', spec);
}

static Walkqid*	 
rtcwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, rtcdir, nelem(rtcdir), devgen);
}

static int	 
rtcstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, rtcdir, nelem(rtcdir), devgen);
}

static Chan*
rtcopen(Chan* c, int omode)
{
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void	 
rtcclose(Chan*)
{
}

Dev fakertcdevtab = {
	'r',
	"rtc",

	devreset,
	rtcinit,
	devshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
};

	mmupid;
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1		/* 1 level cache, don't worry about VCE's */
struct PMMU
{
	Page*	mmul2;
	Page*	mmul2cache;	/* free mmu pages */
};

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/devgpio.c
/*
 * Raspberry Pi (BCM2835) GPIO
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum {
	// GPIO registers
	GPLEV = VIRTIO + 0x200034,
};

enum{
	Qdir = 0,
	Qgpio,
};

Dirtab gpiodir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"gpio",	{Qgpio, 0},	0,	0664,
};

enum {
	// commands
	CMfunc,
	CMset,
	CMpullup,
	CMpulldown,
	CMfloat,
};

static Cmdtab gpiocmd[] = {
	{CMfunc, "function", 3},
	{CMset, "set", 3},
	{CMpullup, "pullup", 2},
	{CMpulldown, "pulldown", 2},
	{CMfloat, "float", 2},
};

static char *funcs[] = { "in", "out", "alt5", "alt4", "alt0",
	"alt1", "alt2", "alt3", "pulse"};
static int ifuncs[] = { Input, Output, Alt5, Alt4, Alt0,
	Alt1, Alt2, Alt3, -1};

static Chan*
gpioattach(char* spec)
{
	return devattach('G', spec);
}

static Walkqid*	 
gpiowalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, gpiodir, nelem(gpiodir), devgen);
}

static int	 
gpiostat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, gpiodir, nelem(gpiodir), devgen);
}

static Chan*
gpioopen(Chan* c, int omode)
{
	return devopen(c, omode, gpiodir, nelem(gpiodir), devgen);
}

static void	 
gpioclose(Chan*)
{
}

static long	 
gpioread(Chan* c, void *buf, long n, vlong)
{
	char lbuf[20];
	char *e;

	USED(c);
	if(c->qid.path == Qdir)
		return devdirread(c, buf, n, gpiodir, nelem(gpiodir), devgen);
	e = lbuf + sizeof(lbuf);
	seprint(lbuf, e, "%08ulx%08ulx", ((ulong *)GPLEV)[1], ((ulong *)GPLEV)[0]);
	return readstr(0, buf, n, lbuf);
}

static long	 
gpiowrite(Chan* c, void *buf, long n, vlong)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	int pin, i;

	if(c->qid.type & QTDIR)
		error(Eperm);
	cb = parsecmd(buf, n);
	if(waserror()) {
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, gpiocmd, nelem(gpiocmd));
	pin = atoi(cb->f[1]);
	switch(ct->index) {
	case CMfunc:
		for(i = 0; i < nelem(funcs); i++)
			if(strcmp(funcs[i], cb->f[2]) == 0)
				break;
		if(i >= nelem(funcs))
			error(Ebadctl);
		if(ifuncs[i] == -1) {
			gpiosel(pin, Output);
			microdelay(2);
			gpiosel(pin, Input);
		}
		else {
			gpiosel(pin, ifuncs[i]);
		}
		break;
	case CMset:
		gpioout(pin, atoi(cb->f[2]));
		break;
	case CMpullup:
		gpiopullup(pin);
		break;
	case CMpulldown:
		gpiopulldown(pin);
		break;
	case CMfloat:
		gpiopulloff(pin);
		break;
	}
	free(cb);
	poperror();
	return n;
}

Dev gpiodevtab = {
	'G',
	"gpio",

	devreset,
	devinit,
	devshutdown,
	gpioattach,
	gpiowalk,
	gpiostat,
	gpioopen,
	devcreate,
	gpioclose,
	gpioread,
	devbread,
	gpiowrite,
	devbwrite,
	devremove,
	devwstat,
};
/*
 * i2c
 *
 * Copyright © 1998, 2003 Vita Nuova Limited.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

typedef struct I2Cdir I2Cdir;

enum{
	Qdir,
	Qdata,
	Qctl,
};

static
Dirtab i2ctab[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"i2cdata",		{Qdata, 0},	256,	0660,
	"i2cctl",		{Qctl, 0},		0,	0660,
};

struct I2Cdir {
	Ref;
	I2Cdev;
	Dirtab	tab[nelem(i2ctab)];
};

static void
i2creset(void)
{
	i2csetup(0);
}

static Chan*
i2cattach(char* spec)
{
	char *s;
	ulong addr;
	I2Cdir *d;
	Chan *c;

	addr = strtoul(spec, &s, 16);
	if(*spec == 0 || *s || addr >= (1<<10))
		error("invalid i2c address");
	d = malloc(sizeof(I2Cdir));
	if(d == nil)
		error(Enomem);
	d->ref = 1;
	d->addr = addr;
	d->salen = 0;
	d->tenbit = addr >= 128;
	memmove(d->tab, i2ctab, sizeof(d->tab));
	sprint(d->tab[1].name, "i2c.%lux.data", addr);
	sprint(d->tab[2].name, "i2c.%lux.ctl", addr);

	c = devattach('J', spec);
	c->aux = d;
	return c;
}

static Walkqid*
i2cwalk(Chan* c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	I2Cdir *d;

	d = c->aux;
	wq = devwalk(c, nc, name, nname, d->tab, nelem(d->tab), devgen);
	if(wq != nil && wq->clone != nil && wq->clone != c)
		incref(d);
	return wq;
}

static int
i2cstat(Chan* c, uchar *dp, int n)
{
	I2Cdir *d;

	d = c->aux;
	return devstat(c, dp, n, d->tab, nelem(d->tab), devgen);
}

static Chan*
i2copen(Chan* c, int omode)
{
	I2Cdir *d;

	d = c->aux;
	return devopen(c, omode, d->tab, nelem(d->tab), devgen);
}

static void
i2cclose(Chan *c)
{
	I2Cdir *d;

	d = c->aux;
	if(decref(d) == 0)
		free(d);
}

static long
i2cread(Chan *c, void *a, long n, vlong offset)
{
	I2Cdir *d;
	char *s, *e;
	ulong len;

	d = c->aux;
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, d->tab, nelem(d->tab), devgen);
	case Qdata:
		len = d->tab[1].length;
		if(offset+n >= len){
			n = len - offset;
			if(n <= 0)
				return 0;
		}
		n = i2crecv(d, a, n, offset);
		break;
	case Qctl:
		s = smalloc(READSTR);
		if(waserror()){
			free(s);
			nexterror();
		}
		e = seprint(s, s+READSTR, "size %lud\n", (ulong)d->tab[1].length);
		if(d->salen)
			e = seprint(e, s+READSTR, "subaddress %d\n", d->salen);
		if(d->tenbit)
			seprint(e, s+READSTR, "a10\n");
		n = readstr(offset, a, n, s);
		poperror();
		free(s);
		return n;
	default:
		n=0;
		break;
	}
	return n;
}

static long
i2cwrite(Chan *c, void *a, long n, vlong offset)
{
	I2Cdir *d;
	long len;
	Cmdbuf *cb;

	USED(offset);
	switch((ulong)c->qid.path){
	case Qdata:
		d = c->aux;
		len = d->tab[1].length;
		if(offset+n >= len){
			n = len - offset;
			if(n <= 0)
				return 0;
		}
		n = i2csend(d, a, n, offset);
		break;
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		if(cb->nf < 1)
			error(Ebadctl);
		d = c->aux;
		if(strcmp(cb->f[0], "subaddress") == 0){
			if(cb->nf > 1){
				len = strtol(cb->f[1], nil, 0);
				if(len <= 0)
					len = 0;
				if(len > 4)
					cmderror(cb, "subaddress too long");
			}else
				len = 1;
			d->salen = len;
		}else if(cb->nf > 1 && strcmp(cb->f[0], "size") == 0){
			len = strtol(cb->f[1], nil, 0);
			if(len < 0)
				cmderror(cb, "size is negative");
			d->tab[1].length = len;
		}else if(strcmp(cb->f[0], "a10") == 0)
			d->tenbit = 1;
		else
			cmderror(cb, "unknown control request");
		poperror();
		free(cb);
		break;
	default:
		error(Ebadusefd);
	}
	return n;
}

Dev i2cdevtab = {
	'J',
	"i2c",

	i2creset,
	devinit,
	devshutdown,
	i2cattach,
	i2cwalk,
	i2cstat,
	i2copen,
	devcreate,
	i2cclose,
	i2cread,
	devbread,
	i2cwrite,
	devbwrite,
	devremove,
	devwstat,
};
/*
 * Maxim DS3231 realtime clock (accessed via rtc)
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum {
	/* DS3231 registers */
	Seconds=	0,
	Minutes=	1,
	Hours=		2,
	Weekday=	3,
	Mday=		4,
	Month=		5,
	Year=		6,
	Nbcd=		7,

	/* Hours register may be in 12-hour or 24-hour mode */
	Twelvehr=	1<<6,
	Pm=		1<<5,

	I2Caddr=	0x68,
	
};

typedef struct Rtc	Rtc;

struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
};

enum{
	Qdir = 0,
	Qrtc,
};

Dirtab rtcdir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"rtc",		{Qrtc, 0},	0,	0664,
};

static ulong rtc2sec(Rtc*);
static void sec2rtc(ulong, Rtc*);

static void
i2cread(uint addr, void *buf, int len)
{
	I2Cdev d;

	d.addr = addr;
	d.tenbit = 0;
	d.salen = 0;
	i2crecv(&d, buf, len, 0);
}

static void
i2cwrite(uint addr, void *buf, int len)
{
	I2Cdev d;

	d.addr = addr;
	d.tenbit = 0;
	d.salen = 0;
	i2csend(&d, buf, len, 0);
}

static void
rtcinit()
{
	i2csetup(0);
}

static Chan*
rtcattach(char* spec)
{
	return devattach('r', spec);
}

static Walkqid*	 
rtcwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, rtcdir, nelem(rtcdir), devgen);
}

static int	 
rtcstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, rtcdir, nelem(rtcdir), devgen);
}

static Chan*
rtcopen(Chan* c, int omode)
{
	char dummy;

	omode = openmode(omode);
	switch((ulong)c->qid.path){
	case Qrtc:
		if(strcmp(up->user, eve)!=0 && omode!=OREAD)
			error(Eperm);
		/* if it's not there, this will throw an error */
		i2cread(I2Caddr, &dummy, 1);
		break;
	}
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void	 
rtcclose(Chan*)
{
}

static int
bcd(int n)
{
	return (n & 0xF) + (10 * (n >> 4));
}

long	 
rtctime(void)
{
	uchar clk[Nbcd];
	Rtc rtc;

	clk[0] = 0;
	i2cwrite(I2Caddr, clk, 1);
	i2cread(I2Caddr, clk, Nbcd);

	/*
	 *  convert from BCD
	 */
	rtc.sec = bcd(clk[Seconds]);
	rtc.min = bcd(clk[Minutes]);
	rtc.hour = bcd(clk[Hours]);
	if(clk[Hours] & Twelvehr){
		rtc.hour = bcd(clk[Hours] & 0x1F);
		if(clk[Hours] & Pm)
			rtc.hour += 12;
	}
	rtc.mday = bcd(clk[Mday]);
	rtc.mon = bcd(clk[Month] & 0x1F);
	rtc.year = bcd(clk[Year]);

	/*
	 *  the world starts jan 1 1970
	 */
	if(rtc.year < 70)
		rtc.year += 2000;
	else
		rtc.year += 1900;
	return rtc2sec(&rtc);
}


static long	 
rtcread(Chan* c, void* buf, long n, vlong off)
{
	ulong t;
	ulong offset = off;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	case Qrtc:
		t = rtctime();
		n = readnum(offset, buf, n, t, 12);
		return n;
	}
	error(Ebadarg);
	return 0;
}

#define PUTBCD(n,o) bcdclock[1+o] = (n % 10) | (((n / 10) % 10)<<4)

static long	 
rtcwrite(Chan* c, void* buf, long n, vlong off)
{
	Rtc rtc;
	ulong secs;
	uchar bcdclock[1+Nbcd];
	char *cp, *ep;
	ulong offset = off;

	if(offset!=0)
		error(Ebadarg);


	switch((ulong)c->qid.path){
	case Qrtc:
		/*
		 *  read the time
		 */
		cp = ep = buf;
		ep += n;
		while(cp < ep){
			if(*cp>='0' && *cp<='9')
				break;
			cp++;
		}
		secs = strtoul(cp, 0, 0);
	
		/*
		 *  convert to bcd
		 */
		sec2rtc(secs, &rtc);
		PUTBCD(rtc.sec, Seconds);
		PUTBCD(rtc.min, Minutes);	/* forces 24 hour mode */
		PUTBCD(rtc.hour, Hours);
		PUTBCD(0, Weekday);		/* hope no other OS uses this */
		PUTBCD(rtc.mday, Mday);
		PUTBCD(rtc.mon, Month);
		PUTBCD(rtc.year, Year);

		/*
		 *  write the clock
		 */
		bcdclock[0] = 0;
		i2cwrite(I2Caddr, bcdclock, 1+Nbcd);
		return n;
	}
	error(Ebadarg);
	return 0;
}

Dev rtc3231devtab = {
	'r',
	"rtc",

	devreset,
	rtcinit,
	devshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
};

#define SEC2MIN 60L
#define SEC2HOUR (60L*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 *  days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
static int*
yrsize(int y)
{
	if((y%4) == 0 && ((y%100) != 0 || (y%400) == 0))
		return ldmsize;
	else
		return dmsize;
}

/*
 *  compute seconds since Jan 1 1970
 */
static ulong
rtc2sec(Rtc *rtc)
{
	ulong secs;
	int i;
	int *d2m;

	secs = 0;

	/*
	 *  seconds per year
	 */
	for(i = 1970; i < rtc->year; i++){
		d2m = yrsize(i);
		secs += d2m[0] * SEC2DAY;
	}

	/*
	 *  seconds per month
	 */
	d2m = yrsize(rtc->year);
	for(i = 1; i < rtc->mon; i++)
		secs += d2m[i] * SEC2DAY;

	secs += (rtc->mday-1) * SEC2DAY;
	secs += rtc->hour * SEC2HOUR;
	secs += rtc->min * SEC2MIN;
	secs += rtc->sec;

	return secs;
}

/*
 *  compute rtc from seconds since Jan 1 1970
 */
static void
sec2rtc(ulong secs, Rtc *rtc)
{
	int d;
	long hms, day;
	int *d2m;

	/*
	 * break initial number into days
	 */
	hms = secs % SEC2DAY;
	day = secs / SEC2DAY;
	if(hms < 0) {
		hms += SEC2DAY;
		day -= 1;
	}

	/*
	 * generate hours:minutes:seconds
	 */
	rtc->sec = hms % 60;
	d = hms / 60;
	rtc->min = d % 60;
	d /= 60;
	rtc->hour = d;

	/*
	 * year number
	 */
	if(day >= 0)
		for(d = 1970; day >= *yrsize(d); d++)
			day -= *yrsize(d);
	else
		for (d = 1970; day < 0; d--)
			day += *yrsize(d-1);
	rtc->year = d;

	/*
	 * generate month
	 */
	d2m = yrsize(rtc->year);
	for(d = 1; day >= d2m[d]; d++)
		day -= d2m[d];
	rtc->mday = day + 1;
	rtc->mon = d;

	return;
}

devspi.c/*
 * minimal spi interface for testing
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define SPIREGS	(VIRTIO+0x204000)

extern int qstate(Queue*);

enum {
	QMAX		= 64*1024,
	Nspislave	= 2,
};

typedef struct Spi Spi;

struct Spi {
	int	csel;
	int	opens;
	QLock;
	Queue	*iq;
	Queue	*oq;
};

Spi spidev[Nspislave];

enum{
	Qdir = 0,
	Qctl,
	Qspi,
};

Dirtab spidir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"spictl",		{Qctl, 0},	0,	0664,
	"spi0",		{Qspi+0, 0},	0,	0664,
	"spi1",		{Qspi+1, 0}, 0, 0664,
};

#define DEVID(path)	((ulong)path - Qspi)

enum {
	CMclock,
	CMmode,
	CMlossi,
};

Cmdtab spitab[] = {
	{CMclock, "clock", 2},
	{CMmode, "mode", 2},
	{CMlossi, "lossi", 1},
};

static void
spikick(void *a)
{
	Block *b;
	Spi *spi;

	spi = a;
	b = qget(spi->oq);
	if(b == nil)
		return;
	if(waserror()){
		freeb(b);
		nexterror();
	}
	spirw(spi->csel, b->rp, BLEN(b));
	qpass(spi->iq, b);
	poperror();
}

static void
spiinit(void)
{
}

static long
spiread(Chan *c, void *a, long n, vlong off)
{
	Spi *spi;
	u32int *sp;
	char *p, *e;
	char buf[256];

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, spidir, nelem(spidir), devgen);

	if(c->qid.path == Qctl) {
		sp = (u32int *)SPIREGS;
		p = buf;
		e = p + sizeof(buf);
		p = seprint(p, e, "CS: %08x\n", sp[0]);
		p = seprint(p, e, "CLK: %08x\n", sp[2]);
		p = seprint(p, e, "DLEN: %08x\n", sp[3]);
		p = seprint(p, e, "LTOH: %08x\n", sp[4]);
		seprint(p, e, "DC: %08x\n", sp[5]);
		return readstr(off, a, n, buf);
	}

	spi = &spidev[DEVID(c->qid.path)];
	n = qread(spi->iq, a, n);

	return n;
}

static long
spiwrite(Chan*c, void *a, long n, vlong)
{
	Spi *spi;
	Cmdbuf *cb;
	Cmdtab *ct;

	if(c->qid.type & QTDIR)
		error(Eperm);

	if(c->qid.path == Qctl) {
		cb = parsecmd(a, n);
		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, spitab, nelem(spitab));
		switch(ct->index) {
		case CMclock:
			spiclock(atoi(cb->f[1]));
			break;
		case CMmode:
			spimode(atoi(cb->f[1]));
			break;
		case CMlossi:
			break;
		}
		poperror();
		return n;
	}

	spi = &spidev[DEVID(c->qid.path)];
	n = qwrite(spi->oq, a, n);

	return n;
}

static Chan*
spiattach(char* spec)
{
	return devattach(L'π', spec);
}

static Walkqid*	 
spiwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, spidir, nelem(spidir), devgen);
}

static int	 
spistat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, spidir, nelem(spidir), devgen);
}

static Chan*
spiopen(Chan* c, int omode)
{
	Spi *spi;

	c = devopen(c, omode, spidir, nelem(spidir), devgen);
	if(c->qid.type & QTDIR)
		return c;

	spi = &spidev[DEVID(c->qid.path)];
	qlock(spi);
	if(spi->opens++ == 0){
		spi->csel = DEVID(c->qid.path);
		if(spi->iq == nil)
			spi->iq = qopen(QMAX, 0, nil, nil);
		else
			qreopen(spi->iq);
		if(spi->oq == nil)
			spi->oq = qopen(QMAX, Qkick, spikick, spi);
		else
			qreopen(spi->oq);
	}
	qunlock(spi);
	c->iounit = qiomaxatomic;
	return c;
}

static void	 
spiclose(Chan *c)
{
	Spi *spi;

	if(c->qid.type & QTDIR)
		return;
	if((c->flag & COPEN) == 0)
		return;
	spi = &spidev[DEVID(c->qid.path)];
	qlock(spi);
	if(--spi->opens == 0){
		qclose(spi->iq);
		qhangup(spi->oq, nil);
		qclose(spi->oq);
	}
	qunlock(spi);
}

Dev spidevtab = {
	L'π',
	"spi",

	devreset,
	spiinit,
	devshutdown,
	spiattach,
	spiwalk,
	spistat,
	spiopen,
	devcreate,
	spiclose,
	spiread,
	devbread,
	spiwrite,
	devbwrite,
	devremove,
	devwstat,
};

devusb.c/*
 * USB device driver framework.
 *
 * This is in charge of providing access to actual HCIs
 * and providing I/O to the various endpoints of devices.
 * A separate user program (usbd) is in charge of
 * enumerating the bus, setting up endpoints and
 * starting devices (also user programs).
 *
 * The interface provided is a violation of the standard:
 * you're welcome.
 *
 * The interface consists of a root directory with several files
 * plus a directory (epN.M) with two files per endpoint.
 * A device is represented by its first endpoint, which
 * is a control endpoint automatically allocated for each device.
 * Device control endpoints may be used to create new endpoints.
 * Devices corresponding to hubs may also allocate new devices,
 * perhaps also hubs. Initially, a hub device is allocated for
 * each controller present, to represent its root hub. Those can
 * never be removed.
 *
 * All endpoints refer to the first endpoint (epN.0) of the device,
 * which keeps per-device information, and also to the HCI used
 * to reach them. Although all endpoints cache that information.
 *
 * epN.M/data files permit I/O and are considered DMEXCL.
 * epN.M/ctl files provide status info and accept control requests.
 *
 * Endpoints may be given file names to be listed also at #u,
 * for those drivers that have nothing to do after configuring the
 * device and its endpoints.
 *
 * Drivers for different controllers are kept at usb[oue]hci.c
 * It's likely we could factor out much from controllers into
 * a generic controller driver, the problem is that details
 * regarding how to handle toggles, tokens, Tds, etc. will
 * get in the way. Thus, code is probably easier the way it is.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"

typedef struct Hcitype Hcitype;

enum
{
	/* Qid numbers */
	Qdir = 0,		/* #u */
	Qusbdir,			/* #u/usb */
	Qctl,			/* #u/usb/ctl - control requests */

	Qep0dir,			/* #u/usb/ep0.0 - endpoint 0 dir */
	Qep0io,			/* #u/usb/ep0.0/data - endpoint 0 I/O */
	Qep0ctl,		/* #u/usb/ep0.0/ctl - endpoint 0 ctl. */
	Qep0dummy,		/* give 4 qids to each endpoint */

	Qepdir = 0,		/* (qid-qep0dir)&3 is one of these */
	Qepio,			/* to identify which file for the endpoint */
	Qepctl,

	/* ... */

	/* Usb ctls. */
	CMdebug = 0,		/* debug on|off */
	CMdump,			/* dump (data structures for debug) */

	/* Ep. ctls */
	CMnew = 0,		/* new nb ctl|bulk|intr|iso r|w|rw (endpoint) */
	CMnewdev,		/* newdev full|low|high portnb (allocate new devices) */
	CMhub,			/* hub (set the device as a hub) */
	CMspeed,		/* speed full|low|high|no */
	CMmaxpkt,		/* maxpkt size */
	CMntds,			/* ntds nb (max nb. of tds per µframe) */
	CMclrhalt,		/* clrhalt (halt was cleared on endpoint) */
	CMpollival,		/* pollival interval (interrupt/iso) */
	CMhz,			/* hz n (samples/sec; iso) */
	CMsamplesz,		/* samplesz n (sample size; iso) */
	CMinfo,			/* info infostr (ke.ep info for humans) */
	CMdetach,		/* detach (abort I/O forever on this ep). */
	CMaddress,		/* address (address is assigned) */
	CMdebugep,		/* debug n (set/clear debug for this ep) */
	CMname,			/* name str (show up as #u/name as well) */
	CMtmout,		/* timeout n (activate timeouts for ep) */
	CMpreset,		/* reset the port */

	/* Hub feature selectors */
	Rportenable	= 1,
	Rportreset	= 4,

};

struct Hcitype
{
	char*	type;
	int	(*reset)(Hci*);
};

#define QID(q)	((int)(q).path)

static Cmdtab usbctls[] =
{
	{CMdebug,	"debug",	2},
	{CMdump,	"dump",		1},
};

static Cmdtab epctls[] =
{
	{CMnew,		"new",		4},
	{CMnewdev,	"newdev",	3},
	{CMhub,		"hub",		1},
	{CMspeed,	"speed",	2},
	{CMmaxpkt,	"maxpkt",	2},
	{CMntds,	"ntds",		2},
	{CMpollival,	"pollival",	2},
	{CMsamplesz,	"samplesz",	2},
	{CMhz,		"hz",		2},
	{CMinfo,	"info",		0},
	{CMdetach,	"detach",	1},
	{CMaddress,	"address",	1},
	{CMdebugep,	"debug",	2},
	{CMclrhalt,	"clrhalt",	1},
	{CMname,	"name",		2},
	{CMtmout,	"timeout",	2},
	{CMpreset,	"reset",	1},
};

static Dirtab usbdir[] =
{
	"ctl",		{Qctl},		0,	0666,
};

char *usbmodename[] =
{
	[OREAD]	"r",
	[OWRITE]	"w",
	[ORDWR]	"rw",
};

static char *ttname[] =
{
	[Tnone]	"none",
	[Tctl]	"control",
	[Tiso]	"iso",
	[Tintr]	"interrupt",
	[Tbulk]	"bulk",
};

static char *spname[] =
{
	[Fullspeed]	"full",
	[Lowspeed]	"low",
	[Highspeed]	"high",
	[Nospeed]	"no",
};

static int	debug;
static Hcitype	hcitypes[Nhcis];
static Hci*	hcis[Nhcis];
static QLock	epslck;		/* add, del, lookup endpoints */
static Ep*	eps[Neps];	/* all endpoints known */
static int	epmax;		/* 1 + last endpoint index used  */
static int	usbidgen;	/* device address generator */

/*
 * Is there something like this in a library? should it be?
 */
char*
seprintdata(char *s, char *se, uchar *d, int n)
{
	int i, l;

	s = seprint(s, se, " %#p[%d]: ", d, n);
	l = n;
	if(l > 10)
		l = 10;
	for(i=0; i<l; i++)
		s = seprint(s, se, " %2.2ux", d[i]);
	if(l < n)
		s = seprint(s, se, "...");
	return s;
}

static int
name2speed(char *name)
{
	int i;

	for(i = 0; i < nelem(spname); i++)
		if(strcmp(name, spname[i]) == 0)
			return i;
	return Nospeed;
}

static int
name2ttype(char *name)
{
	int i;

	for(i = 0; i < nelem(ttname); i++)
		if(strcmp(name, ttname[i]) == 0)
			return i;
	/* may be a std. USB ep. type */
	i = strtol(name, nil, 0);
	switch(i+1){
	case Tctl:
	case Tiso:
	case Tbulk:
	case Tintr:
		return i+1;
	default:
		return Tnone;
	}
}

static int
name2mode(char *mode)
{
	int i;

	for(i = 0; i < nelem(usbmodename); i++)
		if(strcmp(mode, usbmodename[i]) == 0)
			return i;
	return -1;
}

static int
qid2epidx(int q)
{
	q = (q-Qep0dir)/4;
	if(q < 0 || q >= epmax || eps[q] == nil)
		return -1;
	return q;
}

static int
isqtype(int q, int type)
{
	if(q < Qep0dir)
		return 0;
	q -= Qep0dir;
	return (q & 3) == type;
}

void
addhcitype(char* t, int (*r)(Hci*))
{
	static int ntype;

	if(ntype == Nhcis)
		panic("too many USB host interface types");
	hcitypes[ntype].type = t;
	hcitypes[ntype].reset = r;
	ntype++;
}

static char*
seprintep(char *s, char *se, Ep *ep, int all)
{
	static char* dsnames[] = { "config", "enabled", "detached", "reset" };
	Udev *d;
	int i;
	int di;

	d = ep->dev;

	qlock(ep);
	if(waserror()){
		qunlock(ep);
		nexterror();
	}
	di = ep->dev->nb;
	if(all)
		s = seprint(s, se, "dev %d ep %d ", di, ep->nb);
	s = seprint(s, se, "%s", dsnames[ep->dev->state]);
	s = seprint(s, se, " %s", ttname[ep->ttype]);
	assert(ep->mode == OREAD || ep->mode == OWRITE || ep->mode == ORDWR);
	s = seprint(s, se, " %s", usbmodename[ep->mode]);
	s = seprint(s, se, " speed %s", spname[d->speed]);
	s = seprint(s, se, " maxpkt %ld", ep->maxpkt);
	s = seprint(s, se, " pollival %ld", ep->pollival);
	s = seprint(s, se, " samplesz %ld", ep->samplesz);
	s = seprint(s, se, " hz %ld", ep->hz);
	s = seprint(s, se, " hub %d", ep->dev->hub);
	s = seprint(s, se, " port %d", ep->dev->port);
	if(ep->inuse)
		s = seprint(s, se, " busy");
	else
		s = seprint(s, se, " idle");
	if(all){
		s = seprint(s, se, " load %uld", ep->load);
		s = seprint(s, se, " ref %ld addr %#p", ep->ref, ep);
		s = seprint(s, se, " idx %d", ep->idx);
		if(ep->name != nil)
			s = seprint(s, se, " name '%s'", ep->name);
		if(ep->tmout != 0)
			s = seprint(s, se, " tmout");
		if(ep == ep->ep0){
			s = seprint(s, se, " ctlrno %#x", ep->hp->ctlrno);
			s = seprint(s, se, " eps:");
			for(i = 0; i < nelem(d->eps); i++)
				if(d->eps[i] != nil)
					s = seprint(s, se, " ep%d.%d", di, i);
		}
	}
	if(ep->info != nil)
		s = seprint(s, se, "\n%s %s\n", ep->info, ep->hp->type);
	else
		s = seprint(s, se, "\n");
	qunlock(ep);
	poperror();
	return s;
}

static Ep*
epalloc(Hci *hp)
{
	Ep *ep;
	int i;

	ep = smalloc(sizeof(Ep));
	ep->ref = 1;
	qlock(&epslck);
	for(i = 0; i < Neps; i++)
		if(eps[i] == nil)
			break;
	if(i == Neps){
		qunlock(&epslck);
		free(ep);
		print("usb: bug: too few endpoints.\n");
		return nil;
	}
	ep->idx = i;
	if(epmax <= i)
		epmax = i+1;
	eps[i] = ep;
	ep->hp = hp;
	ep->maxpkt = 8;
	ep->ntds = 1;
	ep->samplesz = ep->pollival = ep->hz = 0; /* make them void */
	qunlock(&epslck);
	return ep;
}

static Ep*
getep(int i)
{
	Ep *ep;

	if(i < 0 || i >= epmax || eps[i] == nil)
		return nil;
	qlock(&epslck);
	ep = eps[i];
	if(ep != nil)
		incref(ep);
	qunlock(&epslck);
	return ep;
}

static void
putep(Ep *ep)
{
	Udev *d;

	if(ep != nil && decref(ep) == 0){
		d = ep->dev;
		deprint("usb: ep%d.%d %#p released\n", d->nb, ep->nb, ep);
		qlock(&epslck);
		eps[ep->idx] = nil;
		if(ep->idx == epmax-1)
			epmax--;
		if(ep == ep->ep0 && ep->dev != nil && ep->dev->nb == usbidgen)
			usbidgen--;
		qunlock(&epslck);
		if(d != nil){
			qlock(ep->ep0);
			d->eps[ep->nb] = nil;
			qunlock(ep->ep0);
		}
		if(ep->ep0 != ep){
			putep(ep->ep0);
			ep->ep0 = nil;
		}
		free(ep->info);
		free(ep->name);
		free(ep);
	}
}

static void
dumpeps(void)
{
	int i;
	static char buf[512];
	char *s;
	char *e;
	Ep *ep;

	print("usb dump eps: epmax %d Neps %d (ref=1+ for dump):\n", epmax, Neps);
	for(i = 0; i < epmax; i++){
		s = buf;
		e = buf+sizeof(buf);
		ep = getep(i);
		if(ep != nil){
			if(waserror()){
				putep(ep);
				nexterror();
			}
			s = seprint(s, e, "ep%d.%d ", ep->dev->nb, ep->nb);
			seprintep(s, e, ep, 1);
			print("%s", buf);
			ep->hp->seprintep(buf, e, ep);
			print("%s", buf);
			poperror();
			putep(ep);
		}
	}
	print("usb dump hcis:\n");
	for(i = 0; i < Nhcis; i++)
		if(hcis[i] != nil)
			hcis[i]->dump(hcis[i]);
}

static int
newusbid(Hci *)
{
	int id;

	qlock(&epslck);
	id = ++usbidgen;
	if(id >= 0x7F)
		print("#u: too many device addresses; reuse them more\n");
	qunlock(&epslck);
	return id;
}

/*
 * Create endpoint 0 for a new device
 */
static Ep*
newdev(Hci *hp, int ishub, int isroot)
{
	Ep *ep;
	Udev *d;

	ep = epalloc(hp);
	d = ep->dev = smalloc(sizeof(Udev));
	d->nb = newusbid(hp);
	d->eps[0] = ep;
	ep->nb = 0;
	ep->toggle[0] = ep->toggle[1] = 0;
	d->ishub = ishub;
	d->isroot = isroot;
	if(hp->highspeed != 0)
		d->speed = Highspeed;
	else
		d->speed = Fullspeed;
	d->state = Dconfig;		/* address not yet set */
	ep->dev = d;
	ep->ep0 = ep;			/* no ref counted here */
	ep->ttype = Tctl;
	ep->tmout = Xfertmout;
	ep->mode = ORDWR;
	dprint("newdev %#p ep%d.%d %#p\n", d, d->nb, ep->nb, ep);
	return ep;
}

/*
 * Create a new endpoint for the device
 * accessed via the given endpoint 0.
 */
static Ep*
newdevep(Ep *ep, int i, int tt, int mode)
{
	Ep *nep;
	Udev *d;

	d = ep->dev;
	if(d->eps[i] != nil)
		error("endpoint already in use");
	nep = epalloc(ep->hp);
	incref(ep);
	d->eps[i] = nep;
	nep->nb = i;
	nep->toggle[0] = nep->toggle[1] = 0;
	nep->ep0 = ep;
	nep->dev = ep->dev;
	nep->mode = mode;
	nep->ttype = tt;
	nep->debug = ep->debug;
	/* set defaults */
	switch(tt){
	case Tctl:
		nep->tmout = Xfertmout;
		break;
	case Tintr:
		nep->pollival = 10;
		break;
	case Tiso:
		nep->tmout = Xfertmout;
		nep->pollival = 10;
		nep->samplesz = 4;
		nep->hz = 44100;
		break;
	}
	deprint("newdevep ep%d.%d %#p\n", d->nb, nep->nb, nep);
	return ep;
}

static int
epdataperm(int mode)
{

	switch(mode){
	case OREAD:
		return 0440|DMEXCL;
		break;
	case OWRITE:
		return 0220|DMEXCL;
		break;
	default:
		return 0660|DMEXCL;
	}
}

static int
usbgen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	Dirtab *dir;
	int perm;
	char *se;
	Ep *ep;
	int nb;
	int mode;

	if(0)ddprint("usbgen q %#x s %d...", QID(c->qid), s);
	if(s == DEVDOTDOT){
		if(QID(c->qid) <= Qusbdir){
			mkqid(&q, Qdir, 0, QTDIR);
			devdir(c, q, "#u", 0, eve, 0555, dp);
		}else{
			mkqid(&q, Qusbdir, 0, QTDIR);
			devdir(c, q, "usb", 0, eve, 0555, dp);
		}
		if(0)ddprint("ok\n");
		return 1;
	}

	switch(QID(c->qid)){
	case Qdir:				/* list #u */
		if(s == 0){
			mkqid(&q, Qusbdir, 0, QTDIR);
			devdir(c, q, "usb", 0, eve, 0555, dp);
			if(0)ddprint("ok\n");
			return 1;
		}
		s--;
		if(s < 0 || s >= epmax)
			goto Fail;
		ep = getep(s);
		if(ep == nil || ep->name == nil){
			if(ep != nil)
				putep(ep);
			if(0)ddprint("skip\n");
			return 0;
		}
		if(waserror()){
			putep(ep);
			nexterror();
		}
		mkqid(&q, Qep0io+s*4, 0, QTFILE);
		devdir(c, q, ep->name, 0, eve, epdataperm(ep->mode), dp);
		putep(ep);
		poperror();
		if(0)ddprint("ok\n");
		return 1;

	case Qusbdir:				/* list #u/usb */
	Usbdir:
		if(s < nelem(usbdir)){
			dir = &usbdir[s];
			mkqid(&q, dir->qid.path, 0, QTFILE);
			devdir(c, q, dir->name, dir->length, eve, dir->perm, dp);
			if(0)ddprint("ok\n");
			return 1;
		}
		s -= nelem(usbdir);
		if(s < 0 || s >= epmax)
			goto Fail;
		ep = getep(s);
		if(ep == nil){
			if(0)ddprint("skip\n");
			return 0;
		}
		if(waserror()){
			putep(ep);
			nexterror();
		}
		se = up->genbuf+sizeof(up->genbuf);
		seprint(up->genbuf, se, "ep%d.%d", ep->dev->nb, ep->nb);
		mkqid(&q, Qep0dir+4*s, 0, QTDIR);
		putep(ep);
		poperror();
		devdir(c, q, up->genbuf, 0, eve, 0755, dp);
		if(0)ddprint("ok\n");
		return 1;

	case Qctl:
		s = 0;
		goto Usbdir;

	default:				/* list #u/usb/epN.M */
		nb = qid2epidx(QID(c->qid));
		ep = getep(nb);
		if(ep == nil)
			goto Fail;
		mode = ep->mode;
		putep(ep);
		if(isqtype(QID(c->qid), Qepdir)){
		Epdir:
			switch(s){
			case 0:
				mkqid(&q, Qep0io+nb*4, 0, QTFILE);
				perm = epdataperm(mode);
				devdir(c, q, "data", 0, eve, perm, dp);
				break;
			case 1:
				mkqid(&q, Qep0ctl+nb*4, 0, QTFILE);
				devdir(c, q, "ctl", 0, eve, 0664, dp);
				break;
			default:
				goto Fail;
			}
		}else if(isqtype(QID(c->qid), Qepctl)){
			s = 1;
			goto Epdir;
		}else{
			s = 0;
			goto Epdir;
		}
		if(0)ddprint("ok\n");
		return 1;
	}
Fail:
	if(0)ddprint("fail\n");
	return -1;
}

static Hci*
hciprobe(int cardno, int ctlrno)
{
	Hci *hp;
	char *type;
	char name[64];
	static int epnb = 1;	/* guess the endpoint nb. for the controller */

	ddprint("hciprobe %d %d\n", cardno, ctlrno);
	hp = smalloc(sizeof(Hci));
	hp->ctlrno = ctlrno;

	if(cardno < 0)
		for(cardno = 0; cardno < Nhcis; cardno++){
			if(hcitypes[cardno].type == nil)
				break;
			type = hp->type;
			if(type==nil || *type==0)
				type = "uhci";
			if(cistrcmp(hcitypes[cardno].type, type) == 0)
				break;
		}

	if(cardno >= Nhcis || hcitypes[cardno].type == nil){
		free(hp);
		return nil;
	}
	dprint("%s...", hcitypes[cardno].type);
	if(hcitypes[cardno].reset(hp) < 0){
		free(hp);
		return nil;
	}

	snprint(name, sizeof(name), "usb%s", hcitypes[cardno].type);
	intrenable(hp->irq, hp->interrupt, hp, UNKNOWN, name);

	print("#u/usb/ep%d.0: %s: port %#luX irq %d\n",
		epnb, hcitypes[cardno].type, hp->port, hp->irq);
	epnb++;

	return hp;
}

static void
usbreset(void)
{
	int cardno, ctlrno;
	Hci *hp;

	dprint("usbreset\n");

	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++)
		if((hp = hciprobe(-1, ctlrno)) != nil)
			hcis[ctlrno] = hp;
	cardno = ctlrno = 0;
	while(cardno < Nhcis && ctlrno < Nhcis && hcitypes[cardno].type != nil)
		if(hcis[ctlrno] != nil)
			ctlrno++;
		else{
			hp = hciprobe(cardno, ctlrno);
			if(hp == nil)
				cardno++;
			hcis[ctlrno++] = hp;
		}
	if(hcis[Nhcis-1] != nil)
		print("usbreset: bug: Nhcis too small\n");
}

static void
usbinit(void)
{
	Hci *hp;
	int ctlrno;
	Ep *d;
	char info[40];

	dprint("usbinit\n");
	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++){
		hp = hcis[ctlrno];
		if(hp != nil){
			if(hp->init != nil)
				hp->init(hp);
			d = newdev(hp, 1, 1);		/* new root hub */
			d->dev->state = Denabled;	/* although addr == 0 */
			d->maxpkt = 64;
			snprint(info, sizeof(info), "ports %d", hp->nports);
			kstrdup(&d->info, info);
		}
	}
}

static Chan*
usbattach(char *spec)
{
	return devattach(L'u', spec);
}

static Walkqid*
usbwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, usbgen);
}

static int
usbstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, usbgen);
}

/*
 * µs for the given transfer, for bandwidth allocation.
 * This is a very rough worst case for what 5.11.3
 * of the usb 2.0 spec says.
 * Also, we are using maxpkt and not actual transfer sizes.
 * Only when we are sure we
 * are not exceeding b/w might we consider adjusting it.
 */
static ulong
usbload(int speed, int maxpkt)
{
	enum{ Hostns = 1000, Hubns = 333 };
	ulong l;
	ulong bs;

	l = 0;
	bs = 10UL * maxpkt;
	switch(speed){
	case Highspeed:
		l = 55*8*2 + 2 * (3 + bs) + Hostns;
		break;
	case Fullspeed:
		l = 9107 + 84 * (4 + bs) + Hostns;
		break;
	case Lowspeed:
		l = 64107 + 2 * Hubns + 667 * (3 + bs) + Hostns;
		break;
	default:
		print("usbload: bad speed %d\n", speed);
		/* let it run */
	}
	return l / 1000UL;	/* in µs */
}

static Chan*
usbopen(Chan *c, int omode)
{
	int q;
	Ep *ep;
	int mode;

	mode = openmode(omode);
	q = QID(c->qid);

	if(q >= Qep0dir && qid2epidx(q) < 0)
		error(Eio);
	if(q < Qep0dir || isqtype(q, Qepctl) || isqtype(q, Qepdir))
		return devopen(c, omode, nil, 0, usbgen);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	deprint("usbopen q %#x fid %d omode %d\n", q, c->fid, mode);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	qlock(ep);
	if(ep->inuse){
		qunlock(ep);
		error(Einuse);
	}
	ep->inuse = 1;
	qunlock(ep);
	if(waserror()){
		ep->inuse = 0;
		nexterror();
	}
	if(mode != OREAD && ep->mode == OREAD)
		error(Eperm);
	if(mode != OWRITE && ep->mode == OWRITE)
		error(Eperm);
	if(ep->ttype == Tnone)
		error(Enotconf);
	ep->clrhalt = 0;
	ep->rhrepl = -1;
	if(ep->load == 0)
		ep->load = usbload(ep->dev->speed, ep->maxpkt);
	ep->hp->epopen(ep);

	poperror();	/* ep->inuse */
	poperror();	/* don't putep(): ref kept for fid using the ep. */

	c->mode = mode;
	c->flag |= COPEN;
	c->offset = 0;
	c->aux = nil;	/* paranoia */
	return c;
}

static void
epclose(Ep *ep)
{
	qlock(ep);
	if(waserror()){
		qunlock(ep);
		nexterror();
	}
	if(ep->inuse){
		ep->hp->epclose(ep);
		ep->inuse = 0;
	}
	qunlock(ep);
	poperror();
}

static void
usbclose(Chan *c)
{
	int q;
	Ep *ep;

	q = QID(c->qid);
	if(q < Qep0dir || isqtype(q, Qepctl) || isqtype(q, Qepdir))
		return;

	ep = getep(qid2epidx(q));
	if(ep == nil)
		return;
	deprint("usbclose q %#x fid %d ref %ld\n", q, c->fid, ep->ref);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(c->flag & COPEN){
		free(c->aux);
		c->aux = nil;
		epclose(ep);
		putep(ep);	/* release ref kept since usbopen */
		c->flag &= ~COPEN;
	}
	poperror();
	putep(ep);
}

static long
ctlread(Chan *c, void *a, long n, vlong offset)
{
	int q;
	char *s;
	char *us;
	char *se;
	Ep *ep;
	int i;

	q = QID(c->qid);
	us = s = smalloc(READSTR);
	se = s + READSTR;
	if(waserror()){
		free(us);
		nexterror();
	}
	if(q == Qctl)
		for(i = 0; i < epmax; i++){
			ep = getep(i);
			if(ep != nil){
				if(waserror()){
					putep(ep);
					nexterror();
				}
				s = seprint(s, se, "ep%d.%d ", ep->dev->nb, ep->nb);
				s = seprintep(s, se, ep, 0);
				poperror();
			}
			putep(ep);
		}
	else{
		ep = getep(qid2epidx(q));
		if(ep == nil)
			error(Eio);
		if(waserror()){
			putep(ep);
			nexterror();
		}
		if(c->aux != nil){
			/* After a new endpoint request we read
			 * the new endpoint name back.
			 */
			strecpy(s, se, c->aux);
			free(c->aux);
			c->aux = nil;
		}else
			seprintep(s, se, ep, 0);
		poperror();
		putep(ep);
	}
	n = readstr(offset, a, n, us);
	poperror();
	free(us);
	return n;
}

/*
 * Fake root hub emulation.
 */
static long
rhubread(Ep *ep, void *a, long n)
{
	char *b;

	if(ep->dev->isroot == 0 || ep->nb != 0 || n < 2)
		return -1;
	if(ep->rhrepl < 0)
		return -1;

	b = a;
	memset(b, 0, n);
	PUT2(b, ep->rhrepl);
	ep->rhrepl = -1;
	return n;
}

static long
rhubwrite(Ep *ep, void *a, long n)
{
	uchar *s;
	int cmd;
	int feature;
	int port;
	Hci *hp;

	if(ep->dev == nil || ep->dev->isroot == 0 || ep->nb != 0)
		return -1;
	if(n != Rsetuplen)
		error("root hub is a toy hub");
	ep->rhrepl = -1;
	s = a;
	if(s[Rtype] != (Rh2d|Rclass|Rother) && s[Rtype] != (Rd2h|Rclass|Rother))
		error("root hub is a toy hub");
	hp = ep->hp;
	cmd = s[Rreq];
	feature = GET2(s+Rvalue);
	port = GET2(s+Rindex);
	if(port < 1 || port > hp->nports)
		error("bad hub port number");
	switch(feature){
	case Rportenable:
		ep->rhrepl = hp->portenable(hp, port, cmd == Rsetfeature);
		break;
	case Rportreset:
		ep->rhrepl = hp->portreset(hp, port, cmd == Rsetfeature);
		break;
	case Rgetstatus:
		ep->rhrepl = hp->portstatus(hp, port);
		break;
	default:
		ep->rhrepl = 0;
	}
	return n;
}

static long
usbread(Chan *c, void *a, long n, vlong offset)
{
	int q;
	Ep *ep;
	int nr;

	q = QID(c->qid);

	if(c->qid.type == QTDIR)
		return devdirread(c, a, n, nil, 0, usbgen);

	if(q == Qctl || isqtype(q, Qepctl))
		return ctlread(c, a, n, offset);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->dev->state == Ddetach)
		error(Edetach);
	if(ep->mode == OWRITE || ep->inuse == 0)
		error(Ebadusefd);
	switch(ep->ttype){
	case Tnone:
		error("endpoint not configured");
	case Tctl:
		nr = rhubread(ep, a, n);
		if(nr >= 0){
			n = nr;
			break;
		}
		/* else fall */
	default:
		ddeprint("\nusbread q %#x fid %d cnt %ld off %lld\n",q,c->fid,n,offset);
		n = ep->hp->epread(ep, a, n);
		break;
	}
	poperror();
	putep(ep);
	return n;
}

static long
pow2(int n)
{
	return 1 << n;
}

static void
setmaxpkt(Ep *ep, char* s)
{
	long spp;	/* samples per packet */

	if(ep->dev->speed == Highspeed)
		spp = (ep->hz * ep->pollival * ep->ntds + 7999) / 8000;
	else
		spp = (ep->hz * ep->pollival + 999) / 1000;
	ep->maxpkt = spp * ep->samplesz;
	deprint("usb: %s: setmaxpkt: hz %ld poll %ld"
		" ntds %d %s speed -> spp %ld maxpkt %ld\n", s,
		ep->hz, ep->pollival, ep->ntds, spname[ep->dev->speed],
		spp, ep->maxpkt);
	if(ep->maxpkt > 1024){
		print("usb: %s: maxpkt %ld > 1024. truncating\n", s, ep->maxpkt);
		ep->maxpkt = 1024;
	}
}

/*
 * Many endpoint ctls. simply update the portable representation
 * of the endpoint. The actual controller driver will look
 * at them to setup the endpoints as dictated.
 */
static long
epctl(Ep *ep, Chan *c, void *a, long n)
{
	int i, l, mode, nb, tt;
	char *b, *s;
	Cmdbuf *cb;
	Cmdtab *ct;
	Ep *nep;
	Udev *d;
	static char *Info = "info ";

	d = ep->dev;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, epctls, nelem(epctls));
	if(ct == nil)
		error(Ebadctl);
	i = ct->index;
	if(i == CMnew || i == CMspeed || i == CMhub || i == CMpreset)
		if(ep != ep->ep0)
			error("allowed only on a setup endpoint");
	if(i != CMclrhalt && i != CMdetach && i != CMdebugep && i != CMname)
		if(ep != ep->ep0 && ep->inuse != 0)
			error("must configure before using");
	switch(i){
	case CMnew:
		deprint("usb epctl %s\n", cb->f[0]);
		nb = strtol(cb->f[1], nil, 0);
		if(nb < 0 || nb >= Ndeveps)
			error("bad endpoint number");
		tt = name2ttype(cb->f[2]);
		if(tt == Tnone)
			error("unknown endpoint type");
		mode = name2mode(cb->f[3]);
		if(mode < 0)
			error("unknown i/o mode");
		newdevep(ep, nb, tt, mode);
		break;
	case CMnewdev:
		deprint("usb epctl %s\n", cb->f[0]);
		if(ep != ep->ep0 || d->ishub == 0)
			error("not a hub setup endpoint");
		l = name2speed(cb->f[1]);
		if(l == Nospeed)
			error("speed must be full|low|high");
		nep = newdev(ep->hp, 0, 0);
		nep->dev->speed = l;
		if(nep->dev->speed  != Lowspeed)
			nep->maxpkt = 64;	/* assume full speed */
		nep->dev->hub = d->nb;
		nep->dev->port = atoi(cb->f[2]);
		/* next read request will read
		 * the name for the new endpoint
		 */
		l = sizeof(up->genbuf);
		snprint(up->genbuf, l, "ep%d.%d", nep->dev->nb, nep->nb);
		kstrdup(&c->aux, up->genbuf);
		break;
	case CMhub:
		deprint("usb epctl %s\n", cb->f[0]);
		d->ishub = 1;
		break;
	case CMspeed:
		l = name2speed(cb->f[1]);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(l == Nospeed)
			error("speed must be full|low|high");
		qlock(ep->ep0);
		d->speed = l;
		qunlock(ep->ep0);
		break;
	case CMmaxpkt:
		l = strtoul(cb->f[1], nil, 0);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(l < 1 || l > 1024)
			error("maxpkt not in [1:1024]");
		qlock(ep);
		ep->maxpkt = l;
		qunlock(ep);
		break;
	case CMntds:
		l = strtoul(cb->f[1], nil, 0);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(l < 1 || l > 3)
			error("ntds not in [1:3]");
		qlock(ep);
		ep->ntds = l;
		qunlock(ep);
		break;
	case CMpollival:
		if(ep->ttype != Tintr && ep->ttype != Tiso)
			error("not an intr or iso endpoint");
		l = strtoul(cb->f[1], nil, 0);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(ep->ttype == Tiso ||
		   (ep->ttype == Tintr && ep->dev->speed == Highspeed)){
			if(l < 1 || l > 16)
				error("pollival power not in [1:16]");
			l = pow2(l-1);
		}else
			if(l < 1 || l > 255)
				error("pollival not in [1:255]");
		qlock(ep);
		ep->pollival = l;
		if(ep->ttype == Tiso)
			setmaxpkt(ep, "pollival");
		qunlock(ep);
		break;
	case CMsamplesz:
		if(ep->ttype != Tiso)
			error("not an iso endpoint");
		l = strtoul(cb->f[1], nil, 0);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(l <= 0 || l > 8)
			error("samplesz not in [1:8]");
		qlock(ep);
		ep->samplesz = l;
		setmaxpkt(ep, "samplesz");
		qunlock(ep);
		break;
	case CMhz:
		if(ep->ttype != Tiso)
			error("not an iso endpoint");
		l = strtoul(cb->f[1], nil, 0);
		deprint("usb epctl %s %d\n", cb->f[0], l);
		if(l <= 0 || l > 100000)
			error("hz not in [1:100000]");
		qlock(ep);
		ep->hz = l;
		setmaxpkt(ep, "hz");
		qunlock(ep);
		break;
	case CMclrhalt:
		qlock(ep);
		deprint("usb epctl %s\n", cb->f[0]);
		ep->clrhalt = 1;
		qunlock(ep);
		break;
	case CMinfo:
		deprint("usb epctl %s\n", cb->f[0]);
		l = strlen(Info);
		s = a;
		if(n < l+2 || strncmp(Info, s, l) != 0)
			error(Ebadctl);
		if(n > 1024)
			n = 1024;
		b = smalloc(n);
		memmove(b, s+l, n-l);
		b[n-l] = 0;
		if(b[n-l-1] == '\n')
			b[n-l-1] = 0;
		qlock(ep);
		free(ep->info);
		ep->info = b;
		qunlock(ep);
		break;
	case CMaddress:
		deprint("usb epctl %s\n", cb->f[0]);
		ep->dev->state = Denabled;
		break;
	case CMdetach:
		if(ep->dev->isroot != 0)
			error("can't detach a root hub");
		deprint("usb epctl %s ep%d.%d\n",
			cb->f[0], ep->dev->nb, ep->nb);
		ep->dev->state = Ddetach;
		/* Release file system ref. for its endpoints */
		for(i = 0; i < nelem(ep->dev->eps); i++)
			putep(ep->dev->eps[i]);
		break;
	case CMdebugep:
		if(strcmp(cb->f[1], "on") == 0)
			ep->debug = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			ep->debug = 0;
		else
			ep->debug = strtoul(cb->f[1], nil, 0);
		print("usb: ep%d.%d debug %d\n",
			ep->dev->nb, ep->nb, ep->debug);
		break;
	case CMname:
		deprint("usb epctl %s %s\n", cb->f[0], cb->f[1]);
		validname(cb->f[1], 0);
		kstrdup(&ep->name, cb->f[1]);
		break;
	case CMtmout:
		deprint("usb epctl %s\n", cb->f[0]);
		if(ep->ttype == Tiso || ep->ttype == Tctl)
			error("ctl ignored for this endpoint type");
		ep->tmout = strtoul(cb->f[1], nil, 0);
		if(ep->tmout != 0 && ep->tmout < Xfertmout)
			ep->tmout = Xfertmout;
		break;
	case CMpreset:
		deprint("usb epctl %s\n", cb->f[0]);
		if(ep->ttype != Tctl)
			error("not a control endpoint");
		if(ep->dev->state != Denabled)
			error("forbidden on devices not enabled");
		ep->dev->state = Dreset;
		break;
	default:
		panic("usb: unknown epctl %d", ct->index);
	}
	free(cb);
	poperror();
	return n;
}

static long
usbctl(void *a, long n)
{
	Cmdtab *ct;
	Cmdbuf *cb;
	Ep *ep;
	int i;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, usbctls, nelem(usbctls));
	dprint("usb ctl %s\n", cb->f[0]);
	switch(ct->index){
	case CMdebug:
		if(strcmp(cb->f[1], "on") == 0)
			debug = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			debug = 0;
		else
			debug = strtol(cb->f[1], nil, 0);
		print("usb: debug %d\n", debug);
		for(i = 0; i < epmax; i++)
			if((ep = getep(i)) != nil){
				ep->hp->debug(ep->hp, debug);
				putep(ep);
			}
		break;
	case CMdump:
		dumpeps();
		break;
	}
	free(cb);
	poperror();
	return n;
}

static long
ctlwrite(Chan *c, void *a, long n)
{
	int q;
	Ep *ep;

	q = QID(c->qid);
	if(q == Qctl)
		return usbctl(a, n);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->dev->state == Ddetach)
		error(Edetach);
	if(isqtype(q, Qepctl) && c->aux != nil){
		/* Be sure we don't keep a cloned ep name */
		free(c->aux);
		c->aux = nil;
		error("read, not write, expected");
	}
	n = epctl(ep, c, a, n);
	putep(ep);
	poperror();
	return n;
}

static long
usbwrite(Chan *c, void *a, long n, vlong off)
{
	int nr, q;
	Ep *ep;

	if(c->qid.type == QTDIR)
		error(Eisdir);

	q = QID(c->qid);

	if(q == Qctl || isqtype(q, Qepctl))
		return ctlwrite(c, a, n);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->dev->state == Ddetach)
		error(Edetach);
	if(ep->mode == OREAD || ep->inuse == 0)
		error(Ebadusefd);

	switch(ep->ttype){
	case Tnone:
		error("endpoint not configured");
	case Tctl:
		nr = rhubwrite(ep, a, n);
		if(nr >= 0){
			n = nr;
			break;
		}
		/* else fall */
	default:
		ddeprint("\nusbwrite q %#x fid %d cnt %ld off %lld\n",q, c->fid, n, off);
		ep->hp->epwrite(ep, a, n);
	}
	putep(ep);
	poperror();
	return n;
}

Block*
usbbread(Chan *c, long n, ulong offset)
{
	Block *bp;

	bp = allocb(n);
	if(bp == 0)
		error(Enomem);
	if(waserror()) {
		freeb(bp);
		nexterror();
	}
	bp->wp += usbread(c, bp->wp, n, offset);
	poperror();
	return bp;
}

long
usbbwrite(Chan *c, Block *bp, ulong offset)
{
	long n;

	if(waserror()) {
		freeb(bp);
		nexterror();
	}
	n = usbwrite(c, bp->rp, BLEN(bp), offset);
	poperror();
	freeb(bp);

	return n;
}

void
usbshutdown(void)
{
	Hci *hp;
	int i;

	for(i = 0; i < Nhcis; i++){
		hp = hcis[i];
		if(hp == nil)
			continue;
		if(hp->shutdown == nil)
			print("#u: no shutdown function for %s\n", hp->type);
		else
			hp->shutdown(hp);
	}
}

Dev usbdevtab = {
	L'u',
	"usb",

	usbreset,
	usbinit,
	usbshutdown,
	usbattach,
	usbwalk,
	usbstat,
	usbopen,
	devcreate,
	usbclose,
	usbread,
	usbbread,
	usbwrite,
	usbbwrite,
	devremove,
	devwstat,
};
p->nb != 0)
		return -1;
	if(n != Rsetuplen)
		error("root hub is a toy hub");
	ep->rhrepl = -1;
	s = a;
	if(s[Rtype] != (Rh2d|Rclass|Rother) && s[Rtype] != (Rd2h|Rclass|Rother))
		error("root hub is a toy hub");
	hp = ep->hp;
	cmd = s[Rreq];
	feature = GET2(s+Rvalue);
	port = GET2(s+Rindex);
	if(port < 1 || port > hp->nports)
		error("bad hub port number");
	switch(feature){
	case Rportenable:
		ep->rhrepl = hp->portenable(hp, port, cmd == Rsdma.c/*
 * bcm2835 dma controller
 *
 * simplest to use only channels 0-6
 *	channels 7-14 have reduced functionality
 *	channel 15 is at a weird address
 *	channels 0 and 15 have an "external 128 bit 8 word read FIFO"
 *	  for memory to memory transfers
 *
 * Experiments show that only channels 2-5,11-12 work with mmc
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define DMAREGS	(VIRTIO+0x7000)

#define DBG	if(Dbg)

enum {
	Nchan		= 7,		/* number of dma channels */
	Regsize		= 0x100,	/* size of regs for each chan */
	Cbalign		= 64,		/* control block byte alignment (allow for 64-byte cache on bcm2836) */
	Dbg		= 0,
	
	/* registers for each dma controller */
	Cs		= 0x00>>2,
	Conblkad	= 0x04>>2,
	Ti		= 0x08>>2,
	Sourcead	= 0x0c>>2,
	Destad		= 0x10>>2,
	Txfrlen		= 0x14>>2,
	Stride		= 0x18>>2,
	Nextconbk	= 0x1c>>2,
	Debug		= 0x20>>2,

	/* collective registers */
	Intstatus	= 0xfe0>>2,
	Enable		= 0xff0>>2,

	/* Cs */
	Reset		= 1<<31,
	Abort		= 1<<30,
	Error		= 1<<8,
	Waitwrite	= 1<<6,
	Waitdreq	= 1<<5,
	Paused		= 1<<4,
	Dreq		= 1<<3,
	Int		= 1<<2,
	End		= 1<<1,
	Active		= 1<<0,

	/* Ti */
	Permapshift= 16,
	Srcignore	= 1<<11,
	Srcdreq		= 1<<10,
	Srcwidth128	= 1<<9,
	Srcinc		= 1<<8,
	Destignore	= 1<<7,
	Destdreq	= 1<<6,
	Destwidth128	= 1<<5,
	Destinc		= 1<<4,
	Waitresp	= 1<<3,
	Tdmode		= 1<<1,
	Inten		= 1<<0,

	/* Debug */
	Lite		= 1<<28,
	Clrerrors	= 7<<0,
};

typedef struct Ctlr Ctlr;
typedef struct Cb Cb;

struct Ctlr {
	u32int	*regs;
	Cb	*cb;
	Rendez	r;
	int	dmadone;
};

struct Cb {
	u32int	ti;
	u32int	sourcead;
	u32int	destad;
	u32int	txfrlen;
	u32int	stride;
	u32int	nextconbk;
	u32int	reserved[2];
};

static Ctlr dma[Nchan];
static u32int *dmaregs = (u32int*)DMAREGS;

uintptr
dmaaddr(void *va)
{
	if(PTR2UINT(va)&0x40000000)
		panic("dma address %#p (from%#p)\n", va, getcallerpc(&va));
	return soc.busdram | (PTR2UINT(va) & ~KSEGM);
}

static uintptr
dmaioaddr(void *va)
{
	return soc.busio | (PTR2UINT(va) & ~VIRTIO);
}

static void
dump(char *msg, uchar *p, int n)
{
	print("%s", msg);
	while(n-- > 0)
		print(" %2.2x", *p++);
	print("\n");
}

static void
dumpdregs(char *msg, u32int *r)
{
	int i;

	print("%s: %#p =", msg, r);
	for(i = 0; i < 9; i++)
		print(" %8.8uX", r[i]);
	print("\n");
}

static int
dmadone(void *a)
{
	return ((Ctlr*)a)->dmadone;
}

static void
dmainterrupt(Ureg*, void *a)
{
	Ctlr *ctlr;

	ctlr = a;
	ctlr->regs[Cs] = Int;
	ctlr->dmadone = 1;
	wakeup(&ctlr->r);
}

void
dmastart(int chan, int dev, int dir, void *src, void *dst, int len)
{
	Ctlr *ctlr;
	Cb *cb;
	int ti;

	ctlr = &dma[chan];
	if(ctlr->regs == nil){
		ctlr->regs = (u32int*)(DMAREGS + chan*Regsize);
		ctlr->cb = xspanalloc(sizeof(Cb), Cbalign, 0);
		assert(ctlr->cb != nil);
		dmaregs[Enable] |= 1<<chan;
		ctlr->regs[Cs] = Reset;
		while(ctlr->regs[Cs] & Reset)
			;
		intrenable(IRQDMA(chan), dmainterrupt, ctlr, 0, "dma");
	}
	cb = ctlr->cb;
	ti = 0;
	switch(dir){
	case DmaD2M:
		cachedwbinvse(dst, len);
		ti = Srcdreq | Destinc;
		cb->sourcead = dmaioaddr(src);
		cb->destad = dmaaddr(dst);
		break;
	case DmaM2D:
		cachedwbse(src, len);
		ti = Destdreq | Srcinc;
		cb->sourcead = dmaaddr(src);
		cb->destad = dmaioaddr(dst);
		break;
	case DmaM2M:
		cachedwbse(src, len);
		cachedinvse(dst, len);
		ti = Srcinc | Destinc;
		cb->sourcead = dmaaddr(src);
		cb->destad = dmaaddr(dst);
		break;
	}
	cb->ti = ti | dev<<Permapshift | Inten;
	cb->txfrlen = len;
	cb->stride = 0;
	cb->nextconbk = 0;
	cachedwbse(cb, sizeof(Cb));
	ctlr->regs[Cs] = 0;
	microdelay(1);
	ctlr->regs[Conblkad] = dmaaddr(cb);
	DBG print("dma start: %ux %ux %ux %ux %ux %ux\n",
		cb->ti, cb->sourcead, cb->destad, cb->txfrlen,
		cb->stride, cb->nextconbk);
	DBG print("intstatus %ux\n", dmaregs[Intstatus]);
	dmaregs[Intstatus] = 0;
	ctlr->regs[Cs] = Int;
	coherence();
	microdelay(1);
	DBG dumpdregs("before Active", ctlr->regs);
	ctlr->regs[Cs] = Active;
	DBG dumpdregs("after Active", ctlr->regs);
}

int
dmawait(int chan)
{
	Ctlr *ctlr;
	u32int *r;
	int s;

	ctlr = &dma[chan];
	tsleep(&ctlr->r, dmadone, ctlr, 3000);
	ctlr->dmadone = 0;
	r = ctlr->regs;
	DBG dumpdregs("after sleep", r);
	s = r[Cs];
	if((s & (Active|End|Error)) != End){
		print("dma chan %d %s Cs %ux Debug %ux\n", chan,
			(s&End)? "error" : "timeout", s, r[Debug]);
		r[Cs] = Reset;
		r[Debug] = Clrerrors;
		return -1;
	}
	r[Cs] = Int|End;
	return 0;
}

/*
 * USB host driver for BCM2835
 *	Synopsis DesignWare Core USB 2.0 OTG controller
 *
 * Device register definitions
 */

typedef unsigned int Reg;
typedef struct Dwcregs Dwcregs;
typedef struct Hostchan Hostchan;

enum {
	Maxchans	= 16,	/* actual number of channels in ghwcfg2 */
};

struct Dwcregs {
	/* Core global registers 0x000-0x140 */
	Reg	gotgctl;	/* OTG Control and Status */
	Reg	gotgint;	/* OTG Interrupt */
	Reg	gahbcfg;	/* Core AHB Configuration */
	Reg	gusbcfg;	/* Core USB Configuration */
	Reg	grstctl;	/* Core Reset */
	Reg	gintsts;	/* Core Interrupt */
	Reg	gintmsk;	/* Core Interrupt Mask */
	Reg	grxstsr;	/* Receive Status Queue Read (RO) */
	Reg	grxstsp;	/* Receive Status Queue Read & POP (RO) */
	Reg	grxfsiz;	/* Receive FIFO Size */
	Reg	gnptxfsiz;	/* Non Periodic Transmit FIFO Size */
	Reg	gnptxsts;	/* Non Periodic Transmit FIFO/Queue Status (RO) */
	Reg	gi2cctl;	/* I2C Access */
	Reg	gpvndctl;	/* PHY Vendor Control */
	Reg	ggpio;		/* General Purpose Input/Output */
	Reg	guid;		/* User ID */
	Reg	gsnpsid;	/* Synopsys ID (RO) */
	Reg	ghwcfg1;	/* User HW Config1 (RO) (DEVICE) */
	Reg	ghwcfg2;	/* User HW Config2 (RO) */
	Reg	ghwcfg3;	/* User HW Config3 (RO) */
	Reg	ghwcfg4;	/* User HW Config4 (RO)*/
	Reg	glpmcfg;	/* Core LPM Configuration */
	Reg	gpwrdn;		/* Global PowerDn */
	Reg	gdfifocfg;	/* Global DFIFO SW Config (DEVICE?) */
	Reg	adpctl;		/* ADP Control */
	Reg	reserved0[39];
	Reg	hptxfsiz;	/* Host Periodic Transmit FIFO Size */
	Reg	dtxfsiz[15];	/* Device Periodic Transmit FIFOs (DEVICE) */
	char	pad0[0x400-0x140];

	/* Host global registers 0x400-0x420 */
	Reg	hcfg;		/* Configuration */
	Reg	hfir;		/* Frame Interval */
	Reg	hfnum;		/* Frame Number / Frame Remaining (RO) */
	Reg	reserved1;
	Reg	hptxsts;	/* Periodic Transmit FIFO / Queue Status */
	Reg	haint;		/* All Channels Interrupt */
	Reg	haintmsk;	/* All Channels Interrupt Mask */
	Reg	hflbaddr;	/* Frame List Base Address */
	char	pad1[0x440-0x420];

	/* Host port register 0x440 */
	Reg	hport0;		/* Host Port 0 Control and Status */
	char	pad2[0x500-0x444];

	/* Host channel specific registers 0x500-0x700 */
	struct	Hostchan {
		Reg	hcchar;	/* Characteristic */
		Reg	hcsplt;	/* Split Control */
		Reg	hcint;	/* Interrupt */
		Reg	hcintmsk; /* Interrupt Mask */
		Reg	hctsiz;	/* Transfer Size */
		Reg	hcdma;	/* DMA Address */
		Reg	reserved;
		Reg	hcdmab;	/* DMA Buffer Address */
	} hchan[Maxchans];
	char	pad3[0xE00-0x700];

	/* Power & clock gating control register 0xE00 */
	Reg	pcgcctl;
};

enum {
	/* gotgctl */
	Sesreqscs	= 1<<0,
	Sesreq		= 1<<1,
	Vbvalidoven	= 1<<2,
	Vbvalidovval	= 1<<3,
	Avalidoven	= 1<<4,
	Avalidovval	= 1<<5,
	Bvalidoven	= 1<<6,
	Bvalidovval	= 1<<7,
	Hstnegscs	= 1<<8,
	Hnpreq		= 1<<9,
	Hstsethnpen	= 1<<10,
	Devhnpen	= 1<<11,
	Conidsts	= 1<<16,
	Dbnctime	= 1<<17,
	Asesvld		= 1<<18,
	Bsesvld		= 1<<19,
	Otgver		= 1<<20,
	Multvalidbc	= 0x1F<<22,
	Chirpen		= 1<<27,

	/* gotgint */
	Sesenddet	= 1<<2,
	Sesreqsucstschng= 1<<8,
	Hstnegsucstschng= 1<<9,
	Hstnegdet	= 1<<17,
	Adevtoutchng	= 1<<18,
	Debdone		= 1<<19,
	Mvic		= 1<<20,

	/* gahbcfg */
	Glblintrmsk	= 1<<0,
	/* bits 1:4 redefined for BCM2835 */
	Axiburstlen	= 0x3<<1,
		BURST1		= 3<<1,
		BURST2		= 2<<1,
		BURST3		= 1<<1,
		BURST4		= 0<<1,
	Axiwaitwrites	= 1<<4,
	Dmaenable	= 1<<5,
	Nptxfemplvl	= 1<<7,
		NPTX_HALFEMPTY	= 0<<7,
		NPTX_EMPTY	= 1<<7,
	Ptxfemplvl	= 1<<8,
		PTX_HALFEMPTY	= 0<<8,
		PTX_EMPTY	= 1<<8,
	Remmemsupp	= 1<<21,
	Notialldmawrit	= 1<<22,
	Ahbsingle	= 1<<23,

	/* gusbcfg */
	Toutcal		= 0x7<<0,
	Phyif		= 1<<3,
	Ulpi_utmi_sel	= 1<<4,
	Fsintf		= 1<<5,
		FsUnidir	= 0<<5,
		FsBidir		= 1<<5,
	Physel		= 1<<6,
		PhyHighspeed	= 0<<6,
		PhyFullspeed	= 1<<6,
	Ddrsel		= 1<<7,
	Srpcap		= 1<<8,
	Hnpcap		= 1<<9,
	Usbtrdtim	= 0xf<<10,
		OUsbtrdtim		= 10,
	Phylpwrclksel	= 1<<15,
	Otgutmifssel	= 1<<16,
	Ulpi_fsls	= 1<<17,
	Ulpi_auto_res	= 1<<18,
	Ulpi_clk_sus_m	= 1<<19,
	Ulpi_ext_vbus_drv= 1<<20,
	Ulpi_int_vbus_indicator= 1<<21,
	Term_sel_dl_pulse= 1<<22,
	Indicator_complement= 1<<23,
	Indicator_pass_through= 1<<24,
	Ulpi_int_prot_dis= 1<<25,
	Ic_usb_cap	= 1<<26,
	Ic_traffic_pull_remove= 1<<27,
	Tx_end_delay	= 1<<28,
	Force_host_mode	= 1<<29,
	Force_dev_mode	= 1<<30,

	/* grstctl */
	Csftrst		= 1<<0,
	Hsftrst		= 1<<1,
	Hstfrm		= 1<<2,
	Intknqflsh	= 1<<3,
	Rxfflsh		= 1<<4,
	Txfflsh		= 1<<5,
	Txfnum		= 0x1f<<6,
		TXF_ALL		= 0x10<<6,
	Dmareq		= 1<<30,
	Ahbidle		= 1<<31,

	/* gintsts, gintmsk */
	Curmode		= 1<<0,
		HOSTMODE	= 1<<0,
		DEVMODE		= 0<<0,
	Modemismatch	= 1<<1,
	Otgintr		= 1<<2,
	Sofintr		= 1<<3,
	Rxstsqlvl	= 1<<4,
	Nptxfempty	= 1<<5,
	Ginnakeff	= 1<<6,
	Goutnakeff	= 1<<7,
	Ulpickint	= 1<<8,
	I2cintr		= 1<<9,
	Erlysuspend	= 1<<10,
	Usbsuspend	= 1<<11,
	Usbreset	= 1<<12,
	Enumdone	= 1<<13,
	Isooutdrop	= 1<<14,
	Eopframe	= 1<<15,
	Restoredone	= 1<<16,
	Epmismatch	= 1<<17,
	Inepintr	= 1<<18,
	Outepintr	= 1<<19,
	Incomplisoin	= 1<<20,
	Incomplisoout	= 1<<21,
	Fetsusp		= 1<<22,
	Resetdet	= 1<<23,
	Portintr	= 1<<24,
	Hcintr		= 1<<25,
	Ptxfempty	= 1<<26,
	Lpmtranrcvd	= 1<<27,
	Conidstschng	= 1<<28,
	Disconnect	= 1<<29,
	Sessreqintr	= 1<<30,
	Wkupintr	= 1<<31,

	/* grxsts[rp] */
	Chnum		= 0xf<<0,
	Bcnt		= 0x7ff<<4,
	Dpid		= 0x3<<15,
	Pktsts		= 0xf<<17,
		PKTSTS_IN		= 2<<17,
		PKTSTS_IN_XFER_COMP	= 3<<17,
		PKTSTS_DATA_TOGGLE_ERR	= 5<<17,
		PKTSTS_CH_HALTED	= 7<<17,

	/* hptxfsiz, gnptxfsiz */
	Startaddr	= 0xffff<<0,
	Depth		= 0xffff<<16,
		ODepth		= 16,

	/* gnptxsts */
	Nptxfspcavail	= 0xffff<<0,
	Nptxqspcavail	= 0xff<<16,
	Nptxqtop_terminate= 1<<24,
	Nptxqtop_token	= 0x3<<25,
	Nptxqtop_chnep	= 0xf<<27,

	/* gpvndctl */
	Regdata		= 0xff<<0,
	Vctrl		= 0xff<<8,
	Regaddr16_21	= 0x3f<<16,
	Regwr		= 1<<22,
	Newregreq	= 1<<25,
	Vstsbsy		= 1<<26,
	Vstsdone	= 1<<27,
	Disulpidrvr	= 1<<31,

	/* ggpio */
	Gpi		= 0xffff<<0,
	Gpo		= 0xffff<<16,

	/* ghwcfg2 */
	Op_mode		= 0x7<<0,
		HNP_SRP_CAPABLE_OTG	= 0<<0,
		SRP_ONLY_CAPABLE_OTG	= 1<<0,
		NO_HNP_SRP_CAPABLE	= 2<<0,
		SRP_CAPABLE_DEVICE	= 3<<0,
		NO_SRP_CAPABLE_DEVICE	= 4<<0,
		SRP_CAPABLE_HOST	= 5<<0,
		NO_SRP_CAPABLE_HOST	= 6<<0,
	Architecture	= 0x3<<3,
		SLAVE_ONLY		= 0<<3,
		EXT_DMA			= 1<<3,
		INT_DMA			= 2<<3,
	Point2point	= 1<<5,
	Hs_phy_type	= 0x3<<6,
		PHY_NOT_SUPPORTED	= 0<<6,
		PHY_UTMI		= 1<<6,
		PHY_ULPI		= 2<<6,
		PHY_UTMI_ULPI		= 3<<6,
	Fs_phy_type	= 0x3<<8,
	Num_dev_ep	= 0xf<<10,
	Num_host_chan	= 0xf<<14,
		ONum_host_chan		= 14,
	Perio_ep_supported= 1<<18,
	Dynamic_fifo	= 1<<19,
	Nonperio_tx_q_depth= 0x3<<22,
	Host_perio_tx_q_depth= 0x3<<24,
	Dev_token_q_depth= 0x1f<<26,
	Otg_enable_ic_usb= 1<<31,

	/* ghwcfg3 */
	Xfer_size_cntr_width	= 0xf<<0,
	Packet_size_cntr_width	= 0x7<<4,
	Otg_func		= 1<<7,
	I2c			= 1<<8,
	Vendor_ctrl_if		= 1<<9,
	Optional_features	= 1<<10,
	Synch_reset_type	= 1<<11,
	Adp_supp		= 1<<12,
	Otg_enable_hsic		= 1<<13,
	Bc_support		= 1<<14,
	Otg_lpm_en		= 1<<15,
	Dfifo_depth		= 0xffff<<16,
		ODfifo_depth		= 16,

	/* ghwcfg4 */
	Num_dev_perio_in_ep	= 0xf<<0,
	Power_optimiz		= 1<<4,
	Min_ahb_freq		= 1<<5,
	Hiber			= 1<<6,
	Xhiber			= 1<<7,
	Utmi_phy_data_width	= 0x3<<14,
	Num_dev_mode_ctrl_ep	= 0xf<<16,
	Iddig_filt_en		= 1<<20,
	Vbus_valid_filt_en	= 1<<21,
	A_valid_filt_en		= 1<<22,
	B_valid_filt_en		= 1<<23,
	Session_end_filt_en	= 1<<24,
	Ded_fifo_en		= 1<<25,
	Num_in_eps		= 0xf<<26,
	Desc_dma		= 1<<30,
	Desc_dma_dyn		= 1<<31,

	/* glpmcfg */
	Lpm_cap_en	= 1<<0,
	Appl_resp	= 1<<1,
	Hird		= 0xf<<2,
	Rem_wkup_en	= 1<<6,
	En_utmi_sleep	= 1<<7,
	Hird_thres	= 0x1f<<8,
	Lpm_resp	= 0x3<<13,
	Prt_sleep_sts	= 1<<15,
	Sleep_state_resumeok= 1<<16,
	Lpm_chan_index	= 0xf<<17,
	Retry_count	= 0x7<<21,
	Send_lpm	= 1<<24,
	Retry_count_sts	= 0x7<<25,
	Hsic_connect	= 1<<30,
	Inv_sel_hsic	= 1<<31,

	/* gpwrdn */
	Pmuintsel	= 1<<0,
	Pmuactv		= 1<<1,
	Restore		= 1<<2,
	Pwrdnclmp	= 1<<3,
	Pwrdnrstn	= 1<<4,
	Pwrdnswtch	= 1<<5,
	Dis_vbus	= 1<<6,
	Lnstschng	= 1<<7,
	Lnstchng_msk	= 1<<8,
	Rst_det		= 1<<9,
	Rst_det_msk	= 1<<10,
	Disconn_det	= 1<<11,
	Disconn_det_msk	= 1<<12,
	Connect_det	= 1<<13,
	Connect_det_msk	= 1<<14,
	Srp_det		= 1<<15,
	Srp_det_msk	= 1<<16,
	Sts_chngint	= 1<<17,
	Sts_chngint_msk	= 1<<18,
	Linestate	= 0x3<<19,
	Idsts		= 1<<21,
	Bsessvld	= 1<<22,
	Adp_int		= 1<<23,
	Mult_val_id_bc	= 0x1f<<24,

	/* gdfifocfg */
	Gdfifocfg	= 0xffff<<0,
	Epinfobase	= 0xffff<<16,

	/* adpctl */
	Prb_dschg	= 0x3<<0,
	Prb_delta	= 0x3<<2,
	Prb_per		= 0x3<<4,
	Rtim		= 0x7ff<<6,
	Enaprb		= 1<<17,
	Enasns		= 1<<18,
	Adpres		= 1<<19,
	Adpen		= 1<<20,
	Adp_prb_int	= 1<<21,
	Adp_sns_int	= 1<<22,
	Adp_tmout_int	= 1<<23,
	Adp_prb_int_msk	= 1<<24,
	Adp_sns_int_msk	= 1<<25,
	Adp_tmout_int_msk= 1<<26,
	Ar		= 0x3<<27,

	/* hcfg */
	Fslspclksel	= 0x3<<0,
		HCFG_30_60_MHZ	= 0<<0,
		HCFG_48_MHZ	= 1<<0,
		HCFG_6_MHZ	= 2<<0,
	Fslssupp	= 1<<2,
	Ena32khzs	= 1<<7,
	Resvalid	= 0xff<<8,
	Descdma		= 1<<23,
	Frlisten	= 0x3<<24,
	Modechtimen	= 1<<31,

	/* hfir */
	Frint		= 0xffff<<0,
	Hfirrldctrl	= 1<<16,

	/* hfnum */
	Frnum		= 0xffff<<0,
		MAX_FRNUM 	= 0x3FFF<<0,
	Frrem		= 0xffff<<16,

	/* hptxsts */
	Ptxfspcavail	= 0xffff<<0,
	Ptxqspcavail	= 0xff<<16,
	Ptxqtop_terminate= 1<<24,
	Ptxqtop_token	= 0x3<<25,
	Ptxqtop_chnum	= 0xf<<27,
	Ptxqtop_odd	= 1<<31,

	/* haint, haintmsk */
#define CHANINT(n)	(1<<(n))

	/* hport0 */
	Prtconnsts	= 1<<0,		/* connect status (RO) */
	Prtconndet	= 1<<1,		/* connect detected R/W1C) */
	Prtena		= 1<<2,		/* enable (R/W1C) */
	Prtenchng	= 1<<3,		/* enable/disable change (R/W1C) */
	Prtovrcurract	= 1<<4,		/* overcurrent active (RO) */
	Prtovrcurrchng	= 1<<5,		/* overcurrent change (R/W1C) */
	Prtres		= 1<<6,		/* resume */
	Prtsusp		= 1<<7,		/* suspend */
	Prtrst		= 1<<8,		/* reset */
	Prtlnsts	= 0x3<<10,	/* line state {D+,D-} (RO) */
	Prtpwr		= 1<<12,	/* power on */
	Prttstctl	= 0xf<<13,	/* test */
	Prtspd		= 0x3<<17,	/* speed (RO) */
		HIGHSPEED	= 0<<17,
		FULLSPEED	= 1<<17,
		LOWSPEED	= 2<<17,

	/* hcchar */
	Mps		= 0x7ff<<0,	/* endpoint maximum packet size */
	Epnum		= 0xf<<11,	/* endpoint number */
		OEpnum		= 11,
	Epdir		= 1<<15,	/* endpoint direction */
		Epout		= 0<<15,
		Epin		= 1<<15,
	Lspddev		= 1<<17,	/* device is lowspeed */
	Eptype		= 0x3<<18,	/* endpoint type */
		Epctl		= 0<<18,
		Episo		= 1<<18,
		Epbulk		= 2<<18,
		Epintr		= 3<<18,
	Multicnt	= 0x3<<20,	/* transactions per μframe */
					/* or retries per periodic split */
		OMulticnt	= 20,
	Devaddr		= 0x7f<<22,	/* device address */
		ODevaddr	= 22,
	Oddfrm		= 1<<29,	/* xfer in odd frame (iso/interrupt) */
	Chdis		= 1<<30,	/* channel disable (write 1 only) */
	Chen		= 1<<31,	/* channel enable (write 1 only) */

	/* hcsplt */
	Prtaddr		= 0x7f<<0,	/* port address of recipient */
					/* transaction translator */
	Hubaddr		= 0x7f<<7,	/* dev address of transaction */
					/* translator's hub */
		OHubaddr	= 7,
	Xactpos		= 0x3<<14,	/* payload's position within transaction */
		POS_MID		= 0<<14,		
		POS_END		= 1<<14,
		POS_BEGIN	= 2<<14,
		POS_ALL		= 3<<14, /* all of data (<= 188 bytes) */
	Compsplt	= 1<<16,	/* do complete split */
	Spltena		= 1<<31,	/* channel enabled to do splits */

	/* hcint, hcintmsk */
	Xfercomp	= 1<<0,		/* transfer completed without error */
	Chhltd		= 1<<1,		/* channel halted */
	Ahberr		= 1<<2,		/* AHB dma error */
	Stall		= 1<<3,
	Nak		= 1<<4,
	Ack		= 1<<5,
	Nyet		= 1<<6,
	Xacterr		= 1<<7,	/* transaction error (crc, t/o, bit stuff, eop) */
	Bblerr		= 1<<8,
	Frmovrun	= 1<<9,
	Datatglerr	= 1<<10,
	Bna		= 1<<11,
	Xcs_xact	= 1<<12,
	Frm_list_roll	= 1<<13,

	/* hctsiz */
	Xfersize	= 0x7ffff<<0,	/* expected total bytes */
	Pktcnt		= 0x3ff<<19,	/* expected number of packets */
		OPktcnt		= 19,
	Pid		= 0x3<<29,	/* packet id for initial transaction */
		DATA0		= 0<<29,
		DATA1		= 2<<29,	/* sic */
		DATA2		= 1<<29,	/* sic */
		MDATA		= 3<<29,	/* (non-ctl ep) */
		SETUP		= 3<<29,	/* (ctl ep) */
	Dopng		= 1<<31,	/* do PING protocol */

	/* pcgcctl */
	Stoppclk		= 1<<0,
	Gatehclk		= 1<<1,
	Pwrclmp			= 1<<2,
	Rstpdwnmodule		= 1<<3,
	Enbl_sleep_gating	= 1<<5,
	Phy_in_sleep		= 1<<6,
	Deep_sleep		= 1<<7,
	Resetaftsusp		= 1<<8,
	Restoremode		= 1<<9,
	Enbl_extnd_hiber	= 1<<10,
	Extnd_hiber_pwrclmp	= 1<<11,
	Extnd_hiber_switch	= 1<<12,
	Ess_reg_restored	= 1<<13,
	Prt_clk_sel		= 0x3<<14,
	Port_power		= 1<<16,
	Max_xcvrselect		= 0x3<<17,
	Max_termsel		= 1<<19,
	Mac_dev_addr		= 0x7f<<20,
	P2hd_dev_enum_spd	= 0x3<<27,
	P2hd_prt_spd		= 0x3<<29,
	If_dev_mode		= 1<<31,
};
Host Port 0 Control and Status */
	charemmc.c
    /*
 * bcm2835 external mass media controller (mmc / sd host interface)
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

#define EMMCREGS	(VIRTIO+0x300000)

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

typedef struct Ctlr Ctlr;

struct Ctlr {
	Rendez	r;
	Rendez	cardr;
	int	fastclock;
	ulong	extclk;
	int	appcmd;
};

static Ctlr emmc;

static void mmcinterrupt(Ureg*, void*);

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)EMMCREGS;

	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	microdelay(emmc.fastclock? 2 : 20);
	coherence();
	r[reg] = val;
}

static uint
clkdiv(uint d)
{
	uint v;

	assert(d < 1<<10);
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

	r = (u32int*)EMMCREGS;
	div = emmc.extclk / (freq<<1);
	if(emmc.extclk / (div<<1) > freq)
		div++;
	WR(Control1, clkdiv(div) |
		DTO<<Datatoshift | Clkgendiv | Clken | Clkintlen);
	for(i = 0; i < 1000; i++){
		delay(1);
		if(r[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("emmc: can't set clock to %ud\n", freq);
}

static int
datadone(void*)
{
	int i;

	u32int *r = (u32int*)EMMCREGS;
	i = r[Interrupt];
	return i & (Datadone|Err);
}

static int
cardintready(void*)
{
	int i;

	u32int *r = (u32int*)EMMCREGS;
	i = r[Interrupt];
	return i & Cardintr;
}

static int
emmcinit(void)
{
	u32int *r;
	ulong clk;

	clk = getclkrate(ClkEmmc);
	if(clk == 0){
		clk = Extfreq;
		print("emmc: assuming external clock %lud Mhz\n", clk/1000000);
	}
	emmc.extclk = clk;
	r = (u32int*)EMMCREGS;
	if(0)print("emmc control %8.8ux %8.8ux %8.8ux\n",
		r[Control0], r[Control1], r[Control2]);
	WR(Control1, Srsthc);
	delay(10);
	while(r[Control1] & Srsthc)
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

	r = (u32int*)EMMCREGS;
	ver = r[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
		"Arasan eMMC SD Host Controller %2.2x Version %2.2x",
		ver&0xFF, ver>>8);
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

int
sdiocardintr(int wait)
{
	u32int *r;
	int i;

	r = (u32int*)EMMCREGS;
	WR(Interrupt, Cardintr);
	while(((i = r[Interrupt]) & Cardintr) == 0){
		if(!wait)
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

	r = (u32int*)EMMCREGS;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if(cmd == Switchfunc && !emmc.appcmd)
		c |= Isdata|Card2host;
	if(cmd == IORWextended){
		if(arg & (1<<31))
			c |= Host2card;
		else
			c |= Card2host;
		if((r[Blksizecnt]&0xFFFF0000) != 0x10000)
			c |= Multiblock | Blkcnten;
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if(cmd == GoIdle){
		WR(Control0, r[Control0] & ~(Dwidth4|Hispeed));
		emmcclk(Initfreq);
	}
	if(r[Status] & Cmdinhibit){
		print("emmccmd: need to reset Cmdinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((r[Status] & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("emmccmd: need to reset Datinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	WR(Arg1, arg);
	if((i = (r[Interrupt] & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("emmc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = m->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if(m->ticks-now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr)
			print("emmc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, r[Status]);
		WR(Interrupt, i);
		if(r[Status]&Cmdinhibit){
			WR(Control1, r[Control1]|Srstcmd);
			while(r[Control1]&Srstcmd)
				;
		}
		error(Eio);
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
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		WR(Irpten, r[Irpten]|Datadone|Err);
		tsleep(&emmc.r, datadone, 0, 3000);
		i = r[Interrupt];
		if((i & Datadone) == 0)
			print("emmcio: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			print("emmcio: CMD%d error interrupt %ux\n",
				cmd, r[Interrupt]);
		WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		delay(1);
		emmcclk(SDfreq);
		delay(1);
		emmc.fastclock = 1;
	}
	if(cmd == Setbuswidth){
		if(emmc.appcmd){
			/*
			 * If card bus width changes, change host bus width
			 */
			switch(arg){
			case 0:
				WR(Control0, r[Control0] & ~Dwidth4);
				break;
			case 2:
				WR(Control0, r[Control0] | Dwidth4);
				break;
			}
		}else{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				emmcclk(SDfreqhs);
				delay(1);
			}
		}
	}else if(cmd == IORWdirect && (arg & ~0xFF) == (1<<31|0<<28|7<<9)){
		switch(arg & 0x3){
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

void
emmciosetup(int write, void *buf, int bsize, int bcount)
{
	USED(write);
	USED(buf);
	WR(Blksizecnt, bcount<<16 | bsize);
}

static void
emmcio(int write, uchar *buf, int len)
{
	u32int *r;
	int i;

	r = (u32int*)EMMCREGS;
	assert((len&3) == 0);
	okay(1);
	if(waserror()){
		okay(0);
		nexterror();
	}
	if(write)
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaM2D,
			buf, &r[Data], len);
	else
		dmastart(DmaChanEmmc, DmaDevEmmc, DmaD2M,
			&r[Data], buf, len);
	if(dmawait(DmaChanEmmc) < 0)
		error(Eio);
	if(!write)
		cachedinvse(buf, len);
	WR(Irpten, r[Irpten]|Datadone|Err);
	tsleep(&emmc.r, datadone, 0, 3000);
	i = r[Interrupt]&~Cardintr;
	if((i & Datadone) == 0){
		print("emmcio: %d timeout intr %ux stat %ux\n",
			write, i, r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if(i & Err){
		print("emmcio: %d error intr %ux stat %ux\n",
			write, r[Interrupt], r[Status]);
		WR(Interrupt, i);
		error(Eio);
	}
	if(i)
		WR(Interrupt, i);
	poperror();
	okay(0);
}

static void
mmcinterrupt(Ureg*, void*)
{	
	u32int *r;
	int i;

	r = (u32int*)EMMCREGS;
	i = r[Interrupt];
	if(i&(Datadone|Err))
		wakeup(&emmc.r);
	if(i&Cardintr)
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
etbuswidth	= 6,		/* mmc/sd set bus width command */
	Switchfunc	= 6,		/* mmc/sd switch fuether4330.c                                                                                            664       0       0       136726 13526512635  11066                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
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
#define CACHELINESZ 64	/* temp */

enum{
	SDIODEBUG = 0,
	SBDEBUG = 0,
	EVENTDEBUG = 0,
	VARDEBUG = 0,
	FWDEBUG  = 0,

	Corescansz = 512,
	Uploadsz = 2048,

	Wifichan = 0,		/* default channel */
	Firmwarecmp	= 1,

	ARMcm3		= 0x82A,
	ARM7tdmi	= 0x825,
	ARMcr4		= 0x83E,

	Fn0	= 0,
	Fn1 	= 1,
	Fn2	= 2,
	Fbr1	= 0x100,
	Fbr2	= 0x200,

	/* CCCR */
	Ioenable	= 0x02,
	Ioready		= 0x03,
	Intenable	= 0x04,
	Intpend		= 0x05,
	Ioabort		= 0x06,
	Busifc		= 0x07,
	Capability	= 0x08,
	Blksize		= 0x10,
	Highspeed	= 0x13,

	/* SDIOCommands */
	GO_IDLE_STATE		= 0,
	SEND_RELATIVE_ADDR	= 3,
	IO_SEND_OP_COND		= 5,
	SELECT_CARD		= 7,
	VOLTAGE_SWITCH 		= 11,
	IO_RW_DIRECT 		= 52,
	IO_RW_EXTENDED 		= 53,

	/* SELECT_CARD args */
	Rcashift	= 16,

	/* SEND_OP_COND args */
	Hcs	= 1<<30,	/* host supports SDHC & SDXC */
	V3_3	= 3<<20,	/* 3.2-3.4 volts */
	V2_8	= 3<<15,	/* 2.7-2.9 volts */
	V2_0	= 1<<8,		/* 2.0-2.1 volts */
	S18R	= 1<<24,	/* switch to 1.8V request */

	/* Sonics Silicon Backplane (access to cores on chip) */
	Sbwsize	= 0x8000,
	Sb32bit	= 0x8000,
	Sbaddr	= 0x1000a,
		Enumbase	= 	0x18000000,
	Framectl= 0x1000d,
		Rfhalt		=	0x01,
		Wfhalt		=	0x02,
	Clkcsr	= 0x1000e,
		ForceALP	=	0x01,	/* active low-power clock */
		ForceHT		= 	0x02,	/* high throughput clock */
		ForceILP	=	0x04,	/* idle low-power clock */
		ReqALP		=	0x08,
		ReqHT		=	0x10,
		Nohwreq		=	0x20,
		ALPavail	=	0x40,
		HTavail		=	0x80,
	Pullups	= 0x1000f,
	Wfrmcnt	= 0x10019,
	Rfrmcnt	= 0x1001b,
		
	/* core control regs */
	Ioctrl		= 0x408,
	Resetctrl	= 0x800,

	/* socram regs */
	Coreinfo	= 0x00,
	Bankidx		= 0x10,
	Bankinfo	= 0x40,
	Bankpda		= 0x44,

	/* armcr4 regs */
	Cr4Cap		= 0x04,
	Cr4Bankidx	= 0x40,
	Cr4Bankinfo	= 0x44,
	Cr4Cpuhalt	= 0x20,

	/* chipcommon regs */
	Gpiopullup	= 0x58,
	Gpiopulldown	= 0x5c,
	Chipctladdr	= 0x650,
	Chipctldata	= 0x654,

	/* sdio core regs */
	Intstatus	= 0x20,
		Fcstate		= 1<<4,
		Fcchange	= 1<<5,
		FrameInt	= 1<<6,
		MailboxInt	= 1<<7,
	Intmask		= 0x24,
	Sbmbox		= 0x40,
	Sbmboxdata	= 0x48,
	Hostmboxdata= 0x4c,
		Fwready		= 0x80,

	/* wifi control commands */
	GetVar	= 262,
	SetVar	= 263,

	/* status */
	Disconnected=	0,
	Connecting,
	Connected,
};

typedef struct Ctlr Ctlr;

enum{
	Wpa		= 1,
	Wep		= 2,
	Wpa2		= 3,
	WNameLen	= 32,
	WNKeys		= 4,
	WKeyLen		= 32,
	WMinKeyLen	= 5,
	WMaxKeyLen	= 13,
};

typedef struct WKey WKey;
struct WKey
{
	ushort	len;
	char	dat[WKeyLen];
};

struct Ctlr {
	Ether*	edev;
	QLock	cmdlock;
	QLock	pktlock;
	QLock	tlock;
	QLock	alock;
	Lock	txwinlock;
	Rendez	cmdr;
	Rendez	joinr;
	int	joinstatus;
	int	cryptotype;
	int	chanid;
	char	essid[WNameLen + 1];
	WKey	keys[WNKeys];
	Block	*rsp;
	Block	*scanb;
	int	scansecs;
	int	status;
	int	chipid;
	int	chiprev;
	int	armcore;
	char	*regufile;
	union {
		u32int i;
		uchar c[4];
	} resetvec;
	ulong	chipcommon;
	ulong	armctl;
	ulong	armregs;
	ulong	d11ctl;
	ulong	socramregs;
	ulong	socramctl;
	ulong	sdregs;
	int	sdiorev;
	int	socramrev;
	ulong	socramsize;
	ulong	rambase;
	short	reqid;
	uchar	fcmask;
	uchar	txwindow;
	uchar	txseq;
	uchar	rxseq;
};

enum{
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
	{CMauth,	"auth", 2},
	{CMchannel,	"channel", 2},
	{CMcrypt,	"crypt", 2},
	{CMessid,	"essid", 2},
	{CMkey1,	"key1",	2},
	{CMkey2,	"key1",	2},
	{CMkey3,	"key1",	2},
	{CMkey4,	"key1",	2},
	{CMrxkey,	"rxkey", 3},
	{CMrxkey0,	"rxkey0", 3},
	{CMrxkey1,	"rxkey1", 3},
	{CMrxkey2,	"rxkey2", 3},
	{CMrxkey3,	"rxkey3", 3},
	{CMtxkey,	"txkey", 3},
	{CMdebug,	"debug", 2},
	{CMjoin,	"join", 5},
};

typedef struct Sdpcm Sdpcm;
typedef struct Cmd Cmd;
struct Sdpcm {
	uchar	len[2];
	uchar	lenck[2];
	uchar	seq;
	uchar	chanflg;
	uchar	nextlen;
	uchar	doffset;
	uchar	fcmask;
	uchar	window;
	uchar	version;
	uchar	pad;
};

struct Cmd {
	uchar	cmd[4];
	uchar	len[4];
	uchar	flags[2];
	uchar	id[2];
	uchar	status[4];
};

static char config40181[] = "bcmdhd.cal.40181";
static char config40183[] = "bcmdhd.cal.40183.26MHz";

struct {
	int chipid;
	int chiprev;
	char *fwfile;
	char *cfgfile;
	char *regufile;
} firmware[] = {
	{ 0x4330, 3,	"fw_bcm40183b1.bin", config40183, 0 },
	{ 0x4330, 4,	"fw_bcm40183b2.bin", config40183, 0 },
	{ 43362, 0,	"fw_bcm40181a0.bin", config40181, 0 },
	{ 43362, 1,	"fw_bcm40181a2.bin", config40181, 0 },
	{ 43430, 1,	"brcmfmac43430-sdio.bin", "brcmfmac43430-sdio.txt", 0 },
	{ 0x4345, 6, "brcmfmac43455-sdio.bin", "brcmfmac43455-sdio.txt", "brcmfmac43455-sdio.clm_blob" },
};

static QLock sdiolock;
static int iodebug;

static void etherbcmintr(void *);
static void bcmevent(Ctlr*, uchar*, int);
static void wlscanresult(Ether*, uchar*, int);
static void wlsetvar(Ctlr*, char*, void*, int);

static uchar*
put2(uchar *p, short v)
{
	p[0] = v;
	p[1] = v >> 8;
	return p + 2;
}

static uchar*
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
	return p[0] | p[1]<<8;
}

static ulong
get4(uchar *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
}

static void
dump(char *s, void *a, int n)
{
	int i;
	uchar *p;

	p = a;
	print("%s:", s);
	for(i = 0; i < n; i++)
		print("%c%2.2x", i&15? ' ' : '\n', *p++);
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
	if(waserror()){
		if(SDIODEBUG) print("sdiocmd error: cmd %d arg %lux\n", cmd, arg);
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

	if(waserror())
		return 0;
	r = sdiocmd(cmd, arg);
	poperror();
	return r;
}

static int
sdiord(int fn, int addr)
{
	int r;

	r = sdiocmd(IO_RW_DIRECT, (0<<31)|((fn&7)<<28)|((addr&0x1FFFF)<<9));
	if(r & 0xCF00){
		print("ether4330: sdiord(%x, %x) fail: %2.2ux %2.2ux\n", fn, addr, (r>>8)&0xFF, r&0xFF);
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
	for(retry = 0; retry < 10; retry++){
		r = sdiocmd(IO_RW_DIRECT, (1<<31)|((fn&7)<<28)|((addr&0x1FFFF)<<9)|(data&0xFF));
		if((r & 0xCF00) == 0)
			return;
	}
	print("ether4330: sdiowr(%x, %x, %x) fail: %2.2ux %2.2ux\n", fn, addr, data, (r>>8)&0xFF, r&0xFF);
	error(Eio);
}

static void
sdiorwext(int fn, int write, void *a, int len, int addr, int incr)
{
	int bsize, blk, bcount, m;

	bsize = fn == Fn2? 512 : 64;
	while(len > 0){
		if(len >= 511*bsize){
			blk = 1;
			bcount = 511;
			m = bcount*bsize;
		}else if(len > bsize){
			blk = 1;
			bcount = len/bsize;
			m = bcount*bsize;
		}else{
			blk = 0;
			bcount = len;
			m = bcount;
		}
		qlock(&sdiolock);
		if(waserror()){
			print("ether4330: sdiorwext fail: %s\n", up->errstr);
			qunlock(&sdiolock);
			nexterror();
		}
		if(blk)
			sdio.iosetup(write, a, bsize, bcount);
		else
			sdio.iosetup(write, a, bcount, 1);
		sdiocmd_locked(IO_RW_EXTENDED,
			write<<31 | (fn&7)<<28 | blk<<27 | incr<<26 | (addr&0x1FFFF)<<9 | (bcount&0x1FF));
		sdio.io(write, a, m);
		qunlock(&sdiolock);
		poperror();
		len -= m;
		a = (char*)a + m;
		if(incr)
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
	for(i = 48; i <= 53; i++)
		gpiosel(i, Alt0);
	/* connect emmc to wifi */
	for(i = 34; i <= 39; i++){
		gpiosel(i, Alt3);
		if(i == 34)
			gpiopulloff(i);
		else
			gpiopullup(i);
	}
	sdio.init();
	sdio.enable();
	sdiocmd(GO_IDLE_STATE, 0);
	ocr = trysdiocmd(IO_SEND_OP_COND, 0);
	i = 0;
	while((ocr & (1<<31)) == 0){
		if(++i > 5){
			print("ether4330: no response to sdio access: ocr = %lux\n", ocr);
			error(Eio);
		}
		ocr = trysdiocmd(IO_SEND_OP_COND, V3_3);
		tsleep(&up->sleep, return0, nil, 100);
	}
	rca = sdiocmd(SEND_RELATIVE_ADDR, 0) >> Rcashift;
	sdiocmd(SELECT_CARD, rca << Rcashift);
	sdioset(Fn0, Highspeed, 2);
	sdioset(Fn0, Busifc, 2);	/* bus width 4 */
	sdiowr(Fn0, Fbr1+Blksize, 64);
	sdiowr(Fn0, Fbr1+Blksize+1, 64>>8);
	sdiowr(Fn0, Fbr2+Blksize, 512);
	sdiowr(Fn0, Fbr2+Blksize+1, 512>>8);
	sdioset(Fn0, Ioenable, 1<<Fn1);
	sdiowr(Fn0, Intenable, 0);
	for(i = 0; !(sdiord(Fn0, Ioready) & 1<<Fn1); i++){
		if(i == 10){
			print("ether4330: can't enable SDIO function\n");
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
}

static void
sdioreset(void)
{
	sdiowr(Fn0, Ioabort, 1<<3);	/* reset */
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
	uchar cbuf[2*CACHELINESZ];
	uchar *p;

	p = (uchar*)ROUND((uintptr)cbuf, CACHELINESZ);
	memset(p, 0, 4);
	sdiorwext(fn, 0, p, 4, off|Sb32bit, 1);
	if(SDIODEBUG) print("cfgreadl %lux: %2.2x %2.2x %2.2x %2.2x\n", off, p[0], p[1], p[2], p[3]);
	return p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
}

static void
cfgwritel(int fn, ulong off, u32int data)
{
	uchar cbuf[2*CACHELINESZ];
	uchar *p;
	int retry;

	p = (uchar*)ROUND((uintptr)cbuf, CACHELINESZ);
	put4(p, data);
	if(SDIODEBUG) print("cfgwritel %lux: %2.2x %2.2x %2.2x %2.2x\n", off, p[0], p[1], p[2], p[3]);
	retry = 0;
	while(waserror()){
		print("ether4330: cfgwritel retry %lux %ux\n", off, data);
		sdioabort(fn);
		if(++retry == 3)
			nexterror();
	}
	sdiorwext(fn, 1, p, 4, off|Sb32bit, 1);
	poperror();
}

static void
sbwindow(ulong addr)
{
	addr &= ~(Sbwsize-1);
	cfgw(Sbaddr, addr>>8);
	cfgw(Sbaddr+1, addr>>16);
	cfgw(Sbaddr+2, addr>>24);
}

static void
sbrw(int fn, int write, uchar *buf, int len, ulong off)
{
	int n;
	USED(fn);

	if(waserror()){
		print("ether4330: sbrw err off %lux len %ud\n", off, len);
		nexterror();
	}
	if(write){
		if(len >= 4){
			n = len;
			n &= ~3;
			sdiorwext(Fn1, write, buf, n, off|Sb32bit, 1);
			off += n;
			buf += n;
			len -= n;
		}
		while(len > 0){
			sdiowr(Fn1, off|Sb32bit, *buf);
			off++;
			buf++;
			len--;
		}
	}else{
		if(len >= 4){
			n = len;
			n &= ~3;
			sdiorwext(Fn1, write, buf, n, off|Sb32bit, 1);
			off += n;
			buf += n;
			len -= n;
		}
		while(len > 0){
			*buf = sdiord(Fn1, off|Sb32bit);
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
	if(n == 0)
		n = Sbwsize;
	while(len > 0){
		if(n > len)
			n = len;
		sbwindow(off);
		sbrw(Fn1, write, buf, n, off & (Sbwsize-1));
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
	while(len > 0){
		if(n > len)
			n = ROUND(len, 4);
		retry = 0;
		while(waserror()){
			sdioabort(Fn2);
			if(++retry == 3)
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
	if((cfgreadl(Fn1, regs + Resetctrl) & 1) != 0){
		cfgwritel(Fn1, regs + Ioctrl, 3|ioctl);
		cfgreadl(Fn1, regs + Ioctrl);
		return;
	}
	cfgwritel(Fn1, regs + Ioctrl, 3|pre);
	cfgreadl(Fn1, regs + Ioctrl);
	cfgwritel(Fn1, regs + Resetctrl, 1);
	microdelay(10);
	while((cfgreadl(Fn1, regs + Resetctrl) & 1) == 0)
		;
	cfgwritel(Fn1, regs + Ioctrl, 3|ioctl);
	cfgreadl(Fn1, regs + Ioctrl);
}

static void
sbreset(ulong regs, int pre, int ioctl)
{
	sbdisable(regs, pre, ioctl);
	sbwindow(regs);
	if(SBDEBUG) print("sbreset %#p %#lux %#lux ->", regs,
		cfgreadl(Fn1, regs+Ioctrl), cfgreadl(Fn1, regs+Resetctrl));
	while((cfgreadl(Fn1, regs + Resetctrl) & 1) != 0){
		cfgwritel(Fn1, regs + Resetctrl, 0);
		microdelay(40);
	}
	cfgwritel(Fn1, regs + Ioctrl, 1|ioctl);
	cfgreadl(Fn1, regs + Ioctrl);
	if(SBDEBUG) print("%#lux %#lux\n",
		cfgreadl(Fn1, regs+Ioctrl), cfgreadl(Fn1, regs+Resetctrl));
}

static void
corescan(Ctlr *ctl, ulong r)
{
	uchar *buf;
	int i, coreid, corerev;
	ulong addr;

	buf = sdmalloc(Corescansz);
	if(buf == nil)
		error(Enomem);
	sbmem(0, buf, Corescansz, r);
	coreid = 0;
	corerev = 0;
	for(i = 0; i < Corescansz; i += 4){
		switch(buf[i]&0xF){
		case 0xF:	/* end */
			sdfree(buf);
			return;
		case 0x1:	/* core info */
			if((buf[i+4]&0xF) != 0x1)
				break;
			coreid = (buf[i+1] | buf[i+2]<<8) & 0xFFF;
			i += 4;
			corerev = buf[i+3];
			break;
		case 0x05:	/* address */
			addr = buf[i+1]<<8 | buf[i+2]<<16 | buf[i+3]<<24;
			addr &= ~0xFFF;
			if(SBDEBUG) print("core %x %s %#p\n", coreid, buf[i]&0xC0? "ctl" : "mem", addr);
			switch(coreid){
			case 0x800:
				if((buf[i] & 0xC0) == 0)
					ctl->chipcommon = addr;
				break;
			case ARMcm3:
			case ARM7tdmi:
			case ARMcr4:
				ctl->armcore = coreid;
				if(buf[i] & 0xC0){
					if(ctl->armctl == 0)
						ctl->armctl = addr;
				}else{
					if(ctl->armregs == 0)
						ctl->armregs = addr;
				}
				break;
			case 0x80E:
				if(buf[i] & 0xC0)
					ctl->socramctl = addr;
				else if(ctl->socramregs == 0)
					ctl->socramregs = addr;
				ctl->socramrev = corerev;
				break;
			case 0x829:
				if((buf[i] & 0xC0) == 0)
					ctl->sdregs = addr;
				ctl->sdiorev = corerev;
				break;
			case 0x812:
				if(buf[i] & 0xC0)
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

	if(ctl->armcore == ARMcr4){
		r = ctl->armregs;
		sbwindow(r);
		n = cfgreadl(Fn1, r + Cr4Cap);
		if(SBDEBUG) print("cr4 banks %lux\n", n);
		banks = ((n>>4) & 0xF) + (n & 0xF);
		size = 0;
		for(i = 0; i < banks; i++){
			cfgwritel(Fn1, r + Cr4Bankidx, i);
			n = cfgreadl(Fn1, r + Cr4Bankinfo);
			if(SBDEBUG) print("bank %d reg %lux size %lud\n", i, n, 8192 * ((n & 0x3F) + 1));
			size += 8192 * ((n & 0x3F) + 1);
		}
		ctl->socramsize = size;
		ctl->rambase = 0x198000;
		return;
	}
	if(ctl->socramrev <= 7 || ctl->socramrev == 12){
		print("ether4330: SOCRAM rev %d not supported\n", ctl->socramrev);
		error(Eio);
	}
	sbreset(ctl->socramctl, 0, 0);
	r = ctl->socramregs;
	sbwindow(r);
	n = cfgreadl(Fn1, r + Coreinfo);
	if(SBDEBUG) print("socramrev %d coreinfo %lux\n", ctl->socramrev, n);
	banks = (n>>4) & 0xF;
	size = 0;
	for(i = 0; i < banks; i++){
		cfgwritel(Fn1, r + Bankidx, i);
		n = cfgreadl(Fn1, r + Bankinfo);
		if(SBDEBUG) print("bank %d reg %lux size %lud\n", i, n, 8192 * ((n & 0x3F) + 1));
		size += 8192 * ((n & 0x3F) + 1);
	}
	ctl->socramsize = size;
	ctl->rambase = 0;
	if(ctl->chipid == 43430){
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
	print("ether4330: chip %s rev %ld type %ld\n", buf, (r>>16)&0xF, (r>>28)&0xF);
	switch(chipid){
		case 0x4330:
		case 43362:
		case 43430:
		case 0x4345:
			ctl->chipid = chipid;
			ctl->chiprev = (r>>16)&0xF;
			break;
		default:
			print("ether4330: chipid %#x (%d) not supported\n", chipid, chipid);
			error(Eio);
	}
	r = cfgreadl(Fn1, Enumbase + 63*4);
	corescan(ctl, r);
	if(ctl->armctl == 0 || ctl->d11ctl == 0 ||
	   (ctl->armcore == ARMcm3 && (ctl->socramctl == 0 || ctl->socramregs == 0)))
		error("corescan didn't find essential cores\n");
	if(ctl->armcore == ARMcr4)
		sbreset(ctl->armctl, Cr4Cpuhalt, Cr4Cpuhalt);
	else	
		sbdisable(ctl->armctl, 0, 0);
	sbreset(ctl->d11ctl, 8|4, 4);
	ramscan(ctl);
	if(SBDEBUG) print("ARM %#p D11 %#p SOCRAM %#p,%#p %lud bytes @ %#p\n",
		ctl->armctl, ctl->d11ctl, ctl->socramctl, ctl->socramregs, ctl->socramsize, ctl->rambase);
	cfgw(Clkcsr, 0);
	microdelay(10);
	if(SBDEBUG) print("chipclk: %x\n", cfgr(Clkcsr));
	cfgw(Clkcsr, Nohwreq | ReqALP);
	while((cfgr(Clkcsr) & (HTavail|ALPavail)) == 0)
		microdelay(10);
	cfgw(Clkcsr, Nohwreq | ForceALP);
	microdelay(65);
	if(SBDEBUG) print("chipclk: %x\n", cfgr(Clkcsr));
	cfgw(Pullups, 0);
	sbwindow(ctl->chipcommon);
	cfgwritel(Fn1, ctl->chipcommon + Gpiopullup, 0);
	cfgwritel(Fn1, ctl->chipcommon + Gpiopulldown, 0);
	if(ctl->chipid != 0x4330 && ctl->chipid != 43362)
		return;
	cfgwritel(Fn1, ctl->chipcommon + Chipctladdr, 1);
	if(cfgreadl(Fn1, ctl->chipcommon + Chipctladdr) != 1)
		print("ether4330: can't set Chipctladdr\n");
	else{
		r = cfgreadl(Fn1, ctl->chipcommon + Chipctldata);
		if(SBDEBUG) print("chipcommon PMU (%lux) %lux", cfgreadl(Fn1, ctl->chipcommon + Chipctladdr), r);
		/* set SDIO drive strength >= 6mA */
		r &= ~0x3800;
		if(ctl->chipid == 0x4330)
			r |= 3<<11;
		else
			r |= 7<<11;
		cfgwritel(Fn1, ctl->chipcommon + Chipctldata, r);
		if(SBDEBUG) print("-> %lux (= %lux)\n", r, cfgreadl(Fn1, ctl->chipcommon + Chipctldata));
	}
}

static void
sbenable(Ctlr *ctl)
{
	int i;

	if(SBDEBUG) print("enabling HT clock...");
	cfgw(Clkcsr, 0);
	delay(1);
	cfgw(Clkcsr, ReqHT);
	for(i = 0; (cfgr(Clkcsr) & HTavail) == 0; i++){
		if(i == 50){
			print("ether4330: can't enable HT clock: csr %x\n", cfgr(Clkcsr));
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
	cfgw(Clkcsr, cfgr(Clkcsr) | ForceHT);
	delay(10);
	if(SBDEBUG) print("chipclk: %x\n", cfgr(Clkcsr));
	sbwindow(ctl->sdregs);
	cfgwritel(Fn1, ctl->sdregs + Sbmboxdata, 4 << 16);	/* protocol version */
	cfgwritel(Fn1, ctl->sdregs + Intmask, FrameInt | MailboxInt | Fcchange);
	sdioset(Fn0, Ioenable, 1<<Fn2);
	for(i = 0; !(sdiord(Fn0, Ioready) & 1<<Fn2); i++){
		if(i == 10){
			print("ether4330: can't enable SDIO function 2 - ioready %x\n", sdiord(Fn0, Ioready));
			error(Eio);
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
	sdiowr(Fn0, Intenable, (1<<Fn1) | (1<<Fn2) | 1);
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

	skipping = 0;	/* true if in a comment */
	ep = buf + n;	/* end of input */
	op = buf;	/* end of output */
	lp = buf;	/* start of current output line */
	for(p = buf; p < ep; p++){
		switch(c = *p){
		case '#':
			skipping = 1;
			break;
		case '\0':
		case '\n':
			skipping = 0;
			if(op != lp){
				*op++ = '\0';
				lp = op;
			}
			break;
		case '\r':
			break;
		default:
			if(!skipping)
				*op++ = c;
			break;
		}
	}
	if(!skipping && op != lp)
		*op++ = '\0';
	*op++ = '\0';
	for(n = op - buf; n & 03; n++)
		*op++ = '\0';
	return n;
}

/*
 * Try to find firmware file in /boot or in /sys/lib/firmware.
 * Throw an error if not found.
 */
static Chan*
findfirmware(char *file)
{
	char nbuf[64];
	Chan *c;

	if(!waserror()){
		snprint(nbuf, sizeof nbuf, "/boot/%s", file);
		c = namec(nbuf, Aopen, OREAD, 0);
		poperror();
	}else if(!waserror()){
		snprint(nbuf, sizeof nbuf, "/sys/lib/firmware/%s", file);
		c = namec(nbuf, Aopen, OREAD, 0);
		poperror();
	}else{
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
	if(waserror()){
		cclose(c);
		sdfree(buf);
		sdfree(cbuf);
		nexterror();
	}
	buf = sdmalloc(Uploadsz);
	if(buf == nil)
		error(Enomem);
	if(Firmwarecmp){
		cbuf = sdmalloc(Uploadsz);
		if(cbuf == nil)
			error(Enomem);
	}
	off = 0;
	for(;;){
		n = devtab[c->type]->read(c, buf, Uploadsz, off);
		if(n <= 0)
			break;
		if(isconfig){
			n = condense(buf, n);
			off = ctl->socramsize - n - 4;
		}else if(off == 0)
			memmove(ctl->resetvec.c, buf, sizeof(ctl->resetvec.c));
		while(n&3)
			buf[n++] = 0;
		sbmem(1, buf, n, ctl->rambase + off);
		if(isconfig)
			break;
		off += n;
	}
	if(Firmwarecmp){
		if(FWDEBUG) print("compare...");
		if(!isconfig)
			off = 0;
		for(;;){
			if(!isconfig){
				n = devtab[c->type]->read(c, buf, Uploadsz, off);
				if(n <= 0)
					break;
			while(n&3)
				buf[n++] = 0;
			}
			sbmem(0, cbuf, n, ctl->rambase + off);
			if(memcmp(buf, cbuf, n) != 0){
				print("ether4330: firmware load failed offset %d\n", off);
				error(Eio);
			}
			if(isconfig)
				break;
			off += n;
		}
	}
	if(FWDEBUG) print("\n");
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
	enum {
		Reguhdr = 2+2+4+4,
		Regusz	= 1400,
		Regutyp	= 2,
		Flagclm	= 1<<12,
		Firstpkt= 1<<1,
		Lastpkt	= 1<<2,
	};

	buf = nil;
	c = findfirmware(file);
	if(waserror()){
		cclose(c);
		free(buf);
		nexterror();
	}
	buf = malloc(Reguhdr+Regusz+1);
	if(buf == nil)
		error(Enomem);
	put2(buf+2, Regutyp);
	put2(buf+8, 0);
	off = 0;
	flag = Flagclm | Firstpkt;
	while((flag&Lastpkt) == 0){
		n = devtab[c->type]->read(c, buf+Reguhdr, Regusz+1, off);
		if(n <= 0)
			break;
		if(n == Regusz+1)
			--n;
		else{
			while(n&7)
				buf[Reguhdr+n++] = 0;
			flag |= Lastpkt;
		}
		put2(buf+0, flag);
		put4(buf+4, n);
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
	while(firmware[i].chipid != ctl->chipid ||
		   firmware[i].chiprev != ctl->chiprev){
		if(++i == nelem(firmware)){
			print("ether4330: no firmware for chipid %x (%d) chiprev %d\n",
				ctl->chipid, ctl->chipid, ctl->chiprev);
			error("no firmware");
		}
	}
	ctl->regufile = firmware[i].regufile;
	cfgw(Clkcsr, ReqALP);
	while((cfgr(Clkcsr) & ALPavail) == 0)
		microdelay(10);
	memset(buf, 0, 4);
	sbmem(1, buf, 4, ctl->rambase + ctl->socramsize - 4);
	if(FWDEBUG) print("firmware load...");
	upload(ctl, firmware[i].fwfile, 0);
	if(FWDEBUG) print("config load...");
	n = upload(ctl, firmware[i].cfgfile, 1);
	n /= 4;
	n = (n & 0xFFFF) | (~n << 16);
	put4(buf, n);
	sbmem(1, buf, 4, ctl->rambase + ctl->socramsize - 4);
	if(ctl->armcore == ARMcr4){
		sbwindow(ctl->sdregs);
		cfgwritel(Fn1, ctl->sdregs + Intstatus, ~0);
		if(ctl->resetvec.i != 0){
			if(SBDEBUG) print("%ux\n", ctl->resetvec.i);
			sbmem(1, ctl->resetvec.c, sizeof(ctl->resetvec.c), 0);
		}
		sbreset(ctl->armctl, Cr4Cpuhalt, 0);
	}else
		sbreset(ctl->armctl, 0, 0);
}

/*
 * Communication of data and control packets
 */

void
intwait(Ctlr *ctlr, int wait)
{
	ulong ints, mbox;
	int i;

	if(waserror())
		return;
	for(;;){
		sdiocardintr(wait);
		sbwindow(ctlr->sdregs);
		i = sdiord(Fn0, Intpend);
		if(i == 0){
			tsleep(&up->sleep, return0, 0, 10);
			continue;
		}
		ints = cfgreadl(Fn1, ctlr->sdregs + Intstatus);
		cfgwritel(Fn1, ctlr->sdregs + Intstatus, ints);
		if(0) print("INTS: (%x) %lux -> %lux\n", i, ints, cfgreadl(Fn1, ctlr->sdregs + Intstatus));
		if(ints & MailboxInt){
			mbox = cfgreadl(Fn1, ctlr->sdregs + Hostmboxdata);
			cfgwritel(Fn1, ctlr->sdregs + Sbmbox, 2);	/* ack */
			if(mbox & 0x8)
				print("ether4330: firmware ready\n");
		}
		if(ints & FrameInt)
			break;
	}
	poperror();
}

static Block*
wlreadpkt(Ctlr *ctl)
{
	Block *b;
	Sdpcm *p;
	int len, lenck;

	b = allocb(2048);
	p = (Sdpcm*)b->wp;
	qlock(&ctl->pktlock);
	for(;;){
		packetrw(0, b->wp, sizeof(*p));
		len = p->len[0] | p->len[1]<<8;
		if(len == 0){
			freeb(b);
			b = nil;
			break;
		}
		lenck = p->lenck[0] | p->lenck[1]<<8;
		if(lenck != (len ^ 0xFFFF) ||
		   len < sizeof(*p) || len > 2048){
			print("ether4330: wlreadpkt error len %.4x lenck %.4x\n", len, lenck);
			cfgw(Framectl, Rfhalt);
			while(cfgr(Rfrmcnt+1))
				;
			while(cfgr(Rfrmcnt))
				;
			continue;
		}
		if(len > sizeof(*p))
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
	if(!canqlock(&ctl->tlock))
		return;
	if(waserror()){
		qunlock(&ctl->tlock);
		return;
	}
	for(;;){
		lock(&ctl->txwinlock);
		if(ctl->txseq == ctl->txwindow){
			//print("f");
			unlock(&ctl->txwinlock);
			break;
		}
		if(ctl->fcmask & 1<<2){
			//print("x");
			unlock(&ctl->txwinlock);
			break;
		}
		unlock(&ctl->txwinlock);
		b = qget(edev->oq);
		if(b == nil)
			break;
		off = ((uintptr)b->rp & 3) + sizeof(Sdpcm);
		b = padblock(b, off + 4);
		len = BLEN(b);
		p = (Sdpcm*)b->rp;
		memset(p, 0, off);	/* TODO: refactor dup code */
		put2(p->len, len);
		put2(p->lenck, ~len);
		p->chanflg = 2;
		p->seq = ctl->txseq;
		p->doffset = off;
		put4(b->rp + off, 0x20);	/* BDC header */
		if(iodebug) dump("send", b->rp, len);
		qlock(&ctl->pktlock);
		if(waserror()){
			if(iodebug) print("halt frame %x %x\n", cfgr(Wfrmcnt+1), cfgr(Wfrmcnt+1));
			cfgw(Framectl, Wfhalt);
			while(cfgr(Wfrmcnt+1))
				;
			while(cfgr(Wfrmcnt))
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
	for(;;){
		if(flowstart){
			//print("F");
			flowstart = 0;
			txstart(edev);
		}
		b = wlreadpkt(ctl);
		if(b == nil){
			intwait(ctl, 1);
			continue;
		}
		p = (Sdpcm*)b->rp;
		if(p->window != ctl->txwindow || p->fcmask != ctl->fcmask){
			lock(&ctl->txwinlock);
			if(p->window != ctl->txwindow){
				if(ctl->txseq == ctl->txwindow)
					flowstart = 1;
				ctl->txwindow = p->window;
			}
			if(p->fcmask != ctl->fcmask){
				if((p->fcmask & 1<<2) == 0)
					flowstart = 1;
				ctl->fcmask = p->fcmask;
			}
			unlock(&ctl->txwinlock);
		}
		switch(p->chanflg & 0xF){
		case 0:
			if(iodebug) dump("rsp", b->rp, BLEN(b));
			if(BLEN(b) < sizeof(Sdpcm) + sizeof(Cmd))
				break;
			q = (Cmd*)(b->rp + sizeof(*p));
			if((q->id[0] | q->id[1]<<8) != ctl->reqid)
				break;
			ctl->rsp = b;
			wakeup(&ctl->cmdr);
			continue;
		case 1:
			if(iodebug) dump("event", b->rp, BLEN(b));
			if(BLEN(b) > p->doffset + 4){
				b->rp += p->doffset + 4;	/* skip BDC header */
				bcmevent(ctl, b->rp, BLEN(b));
			}else if(iodebug)
				print("short event %ld %d\n", BLEN(b), p->doffset);
			break;
		case 2:
			if(iodebug) dump("packet", b->rp, BLEN(b));
			b->rp += p->doffset + 4;		/* skip BDC header */
			if(BLEN(b) < ETHERHDRSIZE)
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
	if(edev == nil || ctl->status != Connected)
		return;
	ctl->status = Disconnected;
	/* send eof to aux/wpa */
	for(i = 0; i < edev->nfile; i++){
		f = edev->f[i];
		if(f == nil || f->in == nil || f->inuse == 0 || f->type != 0x888e)
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
	[127] = "bcmc credit support"
};

static char*
evstring(uint event)
{
	static char buf[12];

	if(event >= nelem(eventnames) || eventnames[event] == 0){
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

	if(len < ETHERHDRSIZE + 10 + 46)
		return;
	p += ETHERHDRSIZE + 10;			/* skip bcm_ether header */
	len -= ETHERHDRSIZE + 10;
	flags = nhgets(p + 2);
	event = nhgets(p + 6);
	status = nhgetl(p + 8);
	reason = nhgetl(p + 12);
	if(EVENTDEBUG)
		print("ether4330: [%s] status %ld flags %#x reason %ld\n", 
			evstring(event), status, flags, reason);
	switch(event){
	case 19:	/* E_ROAM */
		if(status == 0)
			break;
	/* fall through */
	case 0:		/* E_SET_SSID */
		ctl->joinstatus = 1 + status;
		wakeup(&ctl->joinr);
		break;
	case 16:	/* E_LINK */
		if(flags&1)	/* link up */
			break;
	/* fall through */
	case 5:		/* E_DEAUTH */
	case 6:		/* E_DEAUTH_IND */
	case 12:	/* E_DISASSOC_IND */
		linkdown(ctl);
		break;
	case 26:	/* E_SCAN_COMPLETE */
		break;
	case 69:	/* E_ESCAN_RESULT */
		wlscanresult(ctl->edev, p + 48, len - 48);
		break;
	default:
		if(status){
			if(!EVENTDEBUG)
				print("ether4330: [%s] error status %ld flags %#x reason %ld\n",
					evstring(event), status, flags, reason);
			dump("event", p, len);
		}
	}
}

static int
joindone(void *a)
{
	return ((Ctlr*)a)->joinstatus;
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
	return ((Ctlr*)a)->rsp != nil;
}

static void
wlcmd(Ctlr *ctl, int write, int op, void *data, int dlen, void *res, int rlen)
{
	Block *b;
	Sdpcm *p;
	Cmd *q;
	int len, tlen;

	if(write)
		tlen = dlen + rlen;
	else
		tlen = MAX(dlen, rlen);
	len = sizeof(Sdpcm) + sizeof(Cmd) + tlen;
	b = allocb(len);
	qlock(&ctl->cmdlock);
	if(waserror()){
		freeb(b);
		qunlock(&ctl->cmdlock);
		nexterror();
	}
	memset(b->wp, 0, len);
	qlock(&ctl->pktlock);
	p = (Sdpcm*)b->wp;
	put2(p->len, len);
	put2(p->lenck, ~len);
	p->seq = ctl->txseq;
	p->doffset = sizeof(Sdpcm);
	b->wp += sizeof(*p);
	
	q = (Cmd*)b->wp;
	put4(q->cmd, op);
	put4(q->len, tlen);
	put2(q->flags, write? 2 : 0);
	put2(q->id, ++ctl->reqid);
	put4(q->status, 0);
	b->wp += sizeof(*q);

	if(dlen > 0)
		memmove(b->wp, data, dlen);
	if(write)
		memmove(b->wp + dlen, res, rlen);
	b->wp += tlen;

	if(iodebug) dump("cmd", b->rp, len);
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
	p = (Sdpcm*)b->rp;
	q = (Cmd*)(b->rp + p->doffset);
	if(q->status[0] | q->status[1] | q->status[2] | q->status[3]){
		print("ether4330: cmd %d error status %ld\n", op, get4(q->status));
		dump("ether4330: cmd error", b->rp, BLEN(b));
		error("wlcmd error");
	}
	if(!write)
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
	if(VARDEBUG){
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
	p = put4(p, i);		/* index */
	p = put4(p, ctl->keys[i].len);
	memmove(p, ctl->keys[i].dat, ctl->keys[i].len);
	p += 32 + 18*4;		/* keydata, pad */
	if(ctl->keys[i].len == WMinKeyLen)
		p = put4(p, 1);		/* algo = WEP1 */
	else
		p = put4(p, 3);		/* algo = WEP128 */
	put4(p, 2);		/* flags = Primarykey */

	wlsetvar(ctl, "wsec_key", params, sizeof params);
}

static void
memreverse(char *dst, char *src, int len)
{
	src += len;
	while(len-- > 0)
		*dst++ = *--src;
}

static void
wlwpakey(Ctlr *ctl, int id, uvlong iv, uchar *ea)
{
	uchar params[164];
	uchar *p;
	int pairwise;

	if(id == CMrxkey)
		return;
	pairwise = (id == CMrxkey || id == CMtxkey);
	memset(params, 0, sizeof params);
	p = params;
	if(pairwise)
		p = put4(p, 0);
	else
		p = put4(p, id - CMrxkey0);	/* group key id */
	p = put4(p, ctl->keys[0].len);
	memmove((char*)p,  ctl->keys[0].dat, ctl->keys[0].len);
	p += 32 + 18*4;		/* keydata, pad */
	if(ctl->cryptotype == Wpa)
		p = put4(p, 2);	/* algo = TKIP */
	else
		p = put4(p, 4);	/* algo = AES_CCM */
	if(pairwise)
		p = put4(p, 0);
	else
		p = put4(p, 2);		/* flags = Primarykey */
	p += 3*4;
	p = put4(p, 0); //pairwise);		/* iv initialised */
	p += 4;
	p = put4(p, iv>>16);	/* iv high */
	p = put2(p, iv&0xFFFF);	/* iv low */
	p += 2 + 2*4;		/* align, pad */
	if(pairwise)
		memmove(p, ea, Eaddrlen);

	wlsetvar(ctl, "wsec_key", params, sizeof params);
}

static void
wljoin(Ctlr *ctl, char *ssid, int chan)
{
	uchar params[72];
	uchar *p;
	int n;

	if(chan != 0)
		chan |= 0x2b00;		/* 20Mhz channel width */
	p = params;
	n = strlen(ssid);
	n = MIN(n, 32);
	p = put4(p, n);
	memmove(p, ssid, n);
	memset(p + n, 0, 32 - n);
	p += 32;
	p = put4(p, 0xff);	/* scan type */
	if(chan != 0){
		p = put4(p, 2);		/* num probes */
		p = put4(p, 120);	/* active time */
		p = put4(p, 390);	/* passive time */
	}else{
		p = put4(p, -1);	/* num probes */
		p = put4(p, -1);	/* active time */
		p = put4(p, -1);	/* passive time */
	}
	p = put4(p, -1);	/* home time */
	memset(p, 0xFF, Eaddrlen);	/* bssid */
	p += Eaddrlen;
	p = put2(p, 0);		/* pad */
	if(chan != 0){
		p = put4(p, 1);		/* num chans */
		p = put2(p, chan);	/* chan spec */
		p = put2(p, 0);		/* pad */
		assert(p == params + sizeof(params));
	}else{
		p = put4(p, 0);		/* num chans */
		assert(p == params + sizeof(params) - 4);
	}

	wlsetvar(ctl, "join", params, chan? sizeof params : sizeof params - 4);
	ctl->status = Connecting;
	switch(waitjoin(ctl)){
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
	static uchar params[4+2+2+4+32+6+1+1+4*4+2+2+14*2+32+4] = {
		1,0,0,0,
		1,0,
		0x34,0x12,
		0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0xff,0xff,0xff,0xff,0xff,0xff,
		2,
		0,
		0xff,0xff,0xff,0xff,
		0xff,0xff,0xff,0xff,
		0xff,0xff,0xff,0xff,
		0xff,0xff,0xff,0xff,
		14,0,
		1,0,
		0x01,0x2b,0x02,0x2b,0x03,0x2b,0x04,0x2b,0x05,0x2e,0x06,0x2e,0x07,0x2e,
		0x08,0x2b,0x09,0x2b,0x0a,0x2b,0x0b,0x2b,0x0c,0x2b,0x0d,0x2b,0x0e,0x2b,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	};

	wlcmdint(ctl, 49, 0);	/* PASSIVE_SCAN */
	wlsetvar(ctl, "escan", params, sizeof params);
}

static uchar*
gettlv(uchar *p, uchar *ep, int tag)
{
	int len;

	while(p + 1 < ep){
		len = p[1];
		if(p + 2 + len > ep)
			return nil;
		if(p[0] == tag)
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
	static uchar wpaie1[4] = { 0x00, 0x50, 0xf2, 0x01 };

	snprint(bssid, sizeof bssid, ";bssid=%E", p + 8);
	if(strstr((char*)bp->rp, bssid) != nil)
		return;
	bp->wp = (uchar*)seprint((char*)bp->wp, (char*)bp->lim,
		"ssid=%.*s%s;signal=%d;noise=%d;chan=%d",
		p[18], (char*)p+19, bssid,
		(short)get2(p+78), (signed char)p[80],
		get2(p+72) & 0xF);
	auth = auth2 = "";
	if(get2(p + 16) & 0x10)
		auth = ";wep";
	ielen = get4(p + 0x78);
	if(ielen > 0){
		t = p + get4(p + 0x74);
		et = t + ielen;
		if(et > p + len)
			return;
		if(gettlv(t, et, 0x30) != nil){
			auth = "";
			auth2 = ";wpa2";
		}
		while((t = gettlv(t, et, 0xdd)) != nil){
			if(t[1] > 4 && memcmp(t+2, wpaie1, 4) == 0){
				auth = ";wpa";
				break;
			}
			t += 2 + t[1];
		}
	}
	bp->wp = (uchar*)seprint((char*)bp->wp, (char*)bp->lim,
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
	if(get4(p) > len)
		return;
	/* TODO: more syntax checking */
	bp = ctlr->scanb;
	if(bp == nil)
		ctlr->scanb = bp = allocb(8192);
	nbss = get2(p+10);
	p += 12;
	len -= 12;
	if(0) dump("SCAN", p, len);
	if(nbss){
		addscan(bp, p, len);
		return;
	}
	i = edev->scan;
	ep = &edev->f[Ntypes];
	for(fp = edev->f; fp < ep && i > 0; fp++){
		f = *fp;
		if(f == nil || f->scan == 0)
			continue;
		if(i == 1)
			qpass(f->in, bp);
		else
			qpass(f->in, copyblock(bp, BLEN(bp)));
		i--;
	}
	if(i)
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
	for(;;){
		tsleep(&up->sleep, return0, 0, 1000);
		if(ctlr->scansecs){
			if(secs == 0){
				if(waserror())
					ctlr->scansecs = 0;
				else{
					wlscanstart(ctlr);
					poperror();
				}
				secs = ctlr->scansecs;
			}
			--secs;
		}else
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
	if(ctlr->chipid == 43430 || ctlr->chipid == 0x4345)
		wlcmdint(ctlr, 0x56, 0);	/* powersave off */
	else
		wlcmdint(ctlr, 0x56, 2);	/* powersave FAST */
	wlsetint(ctlr, "bus:txglom", 0);
	wlsetint(ctlr, "bcn_timeout", 10);
	wlsetint(ctlr, "assoc_retry_max", 3);
	if(ctlr->chipid == 0x4330){
		wlsetint(ctlr, "btc_wire", 4);
		wlsetint(ctlr, "btc_mode", 1);
		wlsetvar(ctlr, "mkeep_alive", keepalive, 11);
	}
	memset(eventmask, 0xFF, sizeof eventmask);
#define ENABLE(n)	eventmask[n/8] |= 1<<(n%8)
#define DISABLE(n)	eventmask[n/8] &= ~(1<<(n%8))
	DISABLE(40);	/* E_RADIO */
	DISABLE(44);	/* E_PROBREQ_MSG */
	DISABLE(54);	/* E_IF */
	DISABLE(71);	/* E_PROBRESP_MSG */
	DISABLE(20);	/* E_TXFAIL */
	DISABLE(124);	/* ? */
	wlsetvar(ctlr, "event_msgs", eventmask, sizeof eventmask);
	wlcmdint(ctlr, 0xb9, 0x28);	/* SET_SCAN_CHANNEL_TIME */
	wlcmdint(ctlr, 0xbb, 0x28);	/* SET_SCAN_UNASSOC_TIME */
	wlcmdint(ctlr, 0x102, 0x82);	/* SET_SCAN_PASSIVE_TIME */
	wlcmdint(ctlr, 2, 0);		/* UP */
	memset(version, 0, sizeof version);
	wlgetvar(ctlr, "ver", version, sizeof version - 1);
	if((p = strchr(version, '\n')) != nil)
		*p = '\0';
	if(0) print("ether4330: %s\n", version);
	wlsetint(ctlr, "roam_off", 1);
	wlcmdint(ctlr, 0x14, 1);	/* SET_INFRA 1 */
	wlcmdint(ctlr, 10, 0);		/* SET_PROMISC */
	//wlcmdint(ctlr, 0x8e, 0);	/* SET_BAND 0 */
	//wlsetint(ctlr, "wsec", 1);
	wlcmdint(ctlr, 2, 1);		/* UP */
	ctlr->keys[0].len = WMinKeyLen;
	//wlwepkey(ctlr, 0);
}

/*
 * Plan 9 driver interface
 */

static long
etherbcmifstat(Ether* edev, void* a, long n, ulong offset)
{
	Ctlr *ctlr;
	char *p;
	int l;
	static char *cryptoname[4] = {
		[0]	"off",
		[Wep]	"wep",
		[Wpa]	"wpa",
		[Wpa2]	"wpa2",
	};
	/* these strings are known by aux/wpa */
	static char* connectstate[] = {
		[Disconnected]	= "unassociated",
		[Connecting] = "connecting",
		[Connected] = "associated",
	};

	ctlr = edev->ctlr;
	if(ctlr == nil)
		return 0;
	p = malloc(READSTR);
	l = 0;

	l += snprint(p+l, READSTR-l, "channel: %d\n", ctlr->chanid);
	l += snprint(p+l, READSTR-l, "essid: %s\n", ctlr->essid);
	l += snprint(p+l, READSTR-l, "crypt: %s\n", cryptoname[ctlr->cryptotype]);
	l += snprint(p+l, READSTR-l, "oq: %d\n", qlen(edev->oq));
	l += snprint(p+l, READSTR-l, "txwin: %d\n", ctlr->txwindow);
	l += snprint(p+l, READSTR-l, "txseq: %d\n", ctlr->txseq);
	l += snprint(p+l, READSTR-l, "status: %s\n", connectstate[ctlr->status]);
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
	if(ctlr == nil)
		return;
	txstart(edev);
}

static int
parsehex(char *buf, int buflen, char *a)
{
	int i, k, n;

	k = 0;
	for(i = 0;k < buflen && *a; i++){
		if(*a >= '0' && *a <= '9')
			n = *a++ - '0';
		else if(*a >= 'a' && *a <= 'f')
			n = *a++ - 'a' + 10;
		else if(*a >= 'A' && *a <= 'F')
			n = *a++ - 'A' + 10;
		else
			break;

		if(i & 1){
			buf[k] |= n;
			k++;
		}
		else
			buf[k] = n<<4;
	}
	if(i & 1)
		return -1;
	return k;
}

static int
wepparsekey(WKey* key, char* a) 
{
	int i, k, len, n;
	char buf[WMaxKeyLen];

	len = strlen(a);
	if(len == WMinKeyLen || len == WMaxKeyLen){
		memset(key->dat, 0, sizeof(key->dat));
		memmove(key->dat, a, len);
		key->len = len;

		return 0;
	}
	else if(len == WMinKeyLen*2 || len == WMaxKeyLen*2){
		k = 0;
		for(i = 0; i < len; i++){
			if(*a >= '0' && *a <= '9')
				n = *a++ - '0';
			else if(*a >= 'a' && *a <= 'f')
				n = *a++ - 'a' + 10;
			else if(*a >= 'A' && *a <= 'F')
				n = *a++ - 'A' + 10;
			else
				return -1;
	
			if(i & 1){
				buf[k] |= n;
				k++;
			}
			else
				buf[k] = n<<4;
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

	if(cistrncmp(a, "tkip:", 5) == 0 || cistrncmp(a, "ccmp:", 5) == 0)
		a += 5;
	else
		return 1;
	len = parsehex(key->dat, sizeof(key->dat), a);
	if(len <= 0)
		return 1;
	key->len = len;
	a += 2*len;
	if(*a++ != '@')
		return 1;
	*ivp = strtoull(a, &e, 16);
	if(e == a)
		return -1;
	return 0;
}

static void
setauth(Ctlr *ctlr, Cmdbuf *cb, char *a)
{
	uchar wpaie[32];
	int i;

	i = parsehex((char*)wpaie, sizeof wpaie, a);
	if(i < 2 || i != wpaie[1] + 2)
		cmderror(cb, "bad wpa ie syntax");
	if(wpaie[0] == 0xdd)
		ctlr->cryptotype = Wpa;
	else if(wpaie[0] == 0x30)
		ctlr->cryptotype = Wpa2;
	else
		cmderror(cb, "bad wpa ie");
	wlsetvar(ctlr, "wpaie", wpaie, i);
	if(ctlr->cryptotype == Wpa){
		wlsetint(ctlr, "wpa_auth", 4|2);	/* auth_psk | auth_unspecified */
		wlsetint(ctlr, "auth", 0);
		wlsetint(ctlr, "wsec", 2);		/* tkip */
		wlsetint(ctlr, "wpa_auth", 4);		/* auth_psk */
	}else{
		wlsetint(ctlr, "wpa_auth", 0x80|0x40);	/* auth_psk | auth_unspecified */
		wlsetint(ctlr, "auth", 0);
		wlsetint(ctlr, "wsec", 4);		/* aes */
		wlsetint(ctlr, "wpa_auth", 0x80);	/* auth_psk */
	}
}

static int
setcrypt(Ctlr *ctlr, Cmdbuf*, char *a)
{
	if(cistrcmp(a, "wep") == 0 || cistrcmp(a, "on") == 0)
		ctlr->cryptotype = Wep;
	else if(cistrcmp(a, "off") == 0 || cistrcmp(a, "none") == 0)
		ctlr->cryptotype = 0;
	else
		return 0;
	wlsetint(ctlr, "auth", ctlr->cryptotype);
	return 1;
}

static long
etherbcmctl(Ether* edev, void* buf, long n)
{
	Ctlr *ctlr;
	Cmdbuf *cb;
	Cmdtab *ct;
	uchar ea[Eaddrlen];
	uvlong iv;
	int i;

	if((ctlr = edev->ctlr) == nil)
		error(Enonexist);
	USED(ctlr);

	cb = parsecmd(buf, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, cmds, nelem(cmds));
	switch(ct->index){
	case CMauth:
		setauth(ctlr, cb, cb->f[1]);
		if(ctlr->essid[0])
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
		break;
	case CMchannel:
		if((i = atoi(cb->f[1])) < 0 || i > 16)
			cmderror(cb, "bad channel number");
		//wlcmdint(ctlr, 30, i);	/* SET_CHANNEL */
		ctlr->chanid = i;
		break;
	case CMcrypt:
		if(setcrypt(ctlr, cb, cb->f[1])){
			if(ctlr->essid[0])
				wljoin(ctlr, ctlr->essid, ctlr->chanid);
		}else
			cmderror(cb, "bad crypt type");
		break;
	case CMessid:
		if(cistrcmp(cb->f[1], "default") == 0)
			memset(ctlr->essid, 0, sizeof(ctlr->essid));
		else{
			strncpy(ctlr->essid, cb->f[1], sizeof(ctlr->essid) - 1);
			ctlr->essid[sizeof(ctlr->essid) - 1] = '\0';
		}
		if(!waserror()){
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
			poperror();
		}
		break;
	case CMjoin:	/* join essid channel wep|on|off|wpakey */
		if(strcmp(cb->f[1], "") != 0){	/* empty string for no change */
			if(cistrcmp(cb->f[1], "default") != 0){
				strncpy(ctlr->essid, cb->f[1], sizeof(ctlr->essid)-1);
				ctlr->essid[sizeof(ctlr->essid)-1] = 0;
			}else
				memset(ctlr->essid, 0, sizeof(ctlr->essid));
		}else if(ctlr->essid[0] == 0)
			cmderror(cb, "essid not set");
		if((i = atoi(cb->f[2])) >= 0 && i <= 16)
			ctlr->chanid = i;
		else
			cmderror(cb, "bad channel number");
		if(!setcrypt(ctlr, cb, cb->f[3]))
			setauth(ctlr, cb, cb->f[3]);
		if(ctlr->essid[0])
			wljoin(ctlr, ctlr->essid, ctlr->chanid);
		break;
	case CMkey1:
	case CMkey2:
	case CMkey3:
	case CMkey4:
		i = ct->index - CMkey1;
		if(wepparsekey(&ctlr->keys[i], cb->f[1]))
			cmderror(cb, "bad WEP key syntax");
		wlsetint(ctlr, "wsec", 1);	/* wep enabled */
		wlwepkey(ctlr, i);
		break;
	case CMrxkey:
	case CMrxkey0:
	case CMrxkey1:
	case CMrxkey2:
	case CMrxkey3:
	case CMtxkey:
		if(parseether(ea, cb->f[1]) < 0)
			cmderror(cb, "bad ether addr");
		if(wpaparsekey(&ctlr->keys[0], &iv, cb->f[2]))
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
	Ether* edev;
	Ctlr* ctlr;

	edev = a;
	ctlr = edev->ctlr;
	ctlr->scansecs = secs;
}

static void
etherbcmattach(Ether* edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	qlock(&ctlr->alock);
	if(waserror()){
		//print("ether4330: attach failed: %s\n", up->errstr);
		qunlock(&ctlr->alock);
		nexterror();
	}
	if(ctlr->edev == nil){
		if(ctlr->chipid == 0){
			sdioinit();
			sbinit(ctlr);
		}
		fwload(ctlr);
		sbenable(ctlr);
		kproc("wifireader", rproc, edev);
		kproc("wifitimer", lproc, edev);
		if(ctlr->regufile)
			reguload(ctlr, ctlr->regufile);
		wlinit(edev, ctlr);
		ctlr->edev = edev;
	}
	qunlock(&ctlr->alock);
	poperror();
}

static void
etherbcmshutdown(Ether*)
{
	sdioreset();
}


static int
etherbcmpnp(Ether* edev)
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

void
ether4330link(void)
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
	u32int	*d;	/* hw descriptor */
	Block	*b;
};

struct Ring
{
	Rendez;
	u32int	*regs;
	u32int	*intregs;
	u32int	intmask;

	Desc	*d;

	u32int	m;
	u32int	cp;
	u32int	rp;
	u32int	wp;

	int	num;
};

struct Ctlr
{
	Lock;
	u32int	*regs;

	Desc	rd[256];
	Desc	td[256];

	Ring	rx[1+0];
	Ring	tx[1+0];

	Rendez	avail[1];
	Rendez	link[1];
	struct {
		Mii;
		Rendez;
	}	mii[1];

	QLock;
	char	attached;
};

static Block *scratch;

#define	REG(x)	(x)

static void
interrupt0(Ureg*, void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	u32int sts;

	sts = REG(ctlr->regs[Intrl0 + IntrSts]) & ~REG(ctlr->regs[Intrl0 + IntrMaskSts]);
	REG(ctlr->regs[Intrl0 + IntrClr]) = sts;
	REG(ctlr->regs[Intrl0 + IntrMaskSet]) = sts;

	if(sts & ctlr->rx->intmask)
		wakeup(ctlr->rx);
	if(sts & ctlr->tx->intmask)
		wakeup(ctlr->tx);

	if(sts & (IrqMdioDone|IrqMdioError))
		wakeup(ctlr->mii);
	if(sts & (IrqLinkUp|IrqLinkDown))
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

	for(i = 1; i < nelem(ctlr->rx); i++)
		if(sts & ctlr->rx[i].intmask)
			wakeup(&ctlr->rx[i]);

	for(i = 1; i < nelem(ctlr->tx); i++)
		if(sts & ctlr->tx[i].intmask)
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
	if(r->rp != r->wp)
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

	while(waserror())
		;

	for(;;){
		if(ctlr->rx->rp == ctlr->rx->wp){
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
		if((s & (DmaSOP|DmaEOP|DmaRxErrors)) != (DmaSOP|DmaEOP)){
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

	return ((r->wp+1) & r->m) != (r->cp & r->m);
}

static void
sendproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	Desc *d;
	Block *b;

	while(waserror())
		;

	for(;;){
		if(!txavail(ctlr->tx)){
			sleep(ctlr->avail, txavail, ctlr->tx);
			continue;
		}
		if((b = qbread(edev->oq, 100000)) == nil)
			break;
		d = &ctlr->tx->d[ctlr->tx->wp & ctlr->tx->m];
		assert(d->b == nil);
		d->b = b;
		cachedwbse(b->rp, BLEN(b));
		setdma(d, b->rp);
		REG(d->d[0]) = BLEN(b)<<16 | DmaTxQtag | DmaSOP | DmaEOP | DmaTxAppendCrc;
		coherence();
		ctlr->tx->wp = (ctlr->tx->wp+1) & 0xFFFF;
		REG(ctlr->tx->regs[TxWP]) = ctlr->tx->wp;
	}
}

static int
txdone(void *arg)
{
	Ring *r = arg;

	if(r->cp != r->wp){
		r->rp = REG(r->regs[TxRP]) & 0xFFFF;
		if(r->cp != r->rp)
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

	while(waserror())
		;

	for(;;){
		if(ctlr->tx->cp == ctlr->tx->rp){
			wakeup(ctlr->avail);
			sleep(ctlr->tx, txdone, ctlr->tx);
			continue;
		}
		d = &ctlr->tx->d[ctlr->tx->cp & ctlr->tx->m];
		assert(d->b != nil);
		freeb(d->b);
		d->b = nil;
		coherence();
		ctlr->tx->cp = (ctlr->tx->cp+1) & 0xFFFF;
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
	REG(ring->regs[DmaStart]) = start*3;
	REG(ring->regs[DmaEnd]) = (start+size)*3 - 1;
	REG(ring->regs[RdmaWP]) = start*3;
	REG(ring->regs[RdmaRP]) = start*3;
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
	REG(ctlr->rx->regs[DmaCtrl]) &= ~(RxRingCfgMask<<1 | DmaCtrlEn);
	REG(ctlr->tx->regs[DmaCtrl]) &= ~(TxRingCfgMask<<1 | DmaCtrlEn);

	REG(ctlr->regs[UmacTxFlush]) = 1;
	microdelay(10);
	REG(ctlr->regs[UmacTxFlush]) = 0;

	while((REG(ctlr->rx->regs[DmaStatus]) & DmaStatusDis) == 0)
		microdelay(10);
	while((REG(ctlr->tx->regs[DmaStatus]) & DmaStatusDis) == 0)
		microdelay(10);
}

static void
dmaon(Ctlr *ctlr)
{
	REG(ctlr->rx->regs[DmaCtrl]) |= DmaCtrlEn;
	REG(ctlr->tx->regs[DmaCtrl]) |= DmaCtrlEn;

	while(REG(ctlr->rx->regs[DmaStatus]) & DmaStatusDis)
		microdelay(10);
	while(REG(ctlr->tx->regs[DmaStatus]) & DmaStatusDis)
		microdelay(10);
}

static void
allocbufs(Ctlr *ctlr)
{
	int i;

	if(scratch == nil){
		scratch = allocb(Rbsz);
		memset(scratch->rp, 0xFF, Rbsz);
		cachedwbse(scratch->rp, Rbsz);
	}

	for(i = 0; i < nelem(ctlr->rd); i++){
		ctlr->rd[i].d = &ctlr->regs[RdmaOffset + i*3];
		replenish(&ctlr->rd[i]);
	}

	for(i = 0; i < nelem(ctlr->td); i++){
		ctlr->td[i].d = &ctlr->regs[TdmaOffset + i*3];
		setdma(&ctlr->td[i], scratch->rp);
		REG(ctlr->td[i].d[0]) = DmaTxUnderrun;
	}
}

static void
freebufs(Ctlr *ctlr)
{
	int i;

	for(i = 0; i < nelem(ctlr->rd); i++){
		if(ctlr->rd[i].b != nil){
			freeb(ctlr->rd[i].b);
			ctlr->rd[i].b = nil;
		}
	}
	for(i = 0; i < nelem(ctlr->td); i++){
		if(ctlr->td[i].b != nil){
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
	rcfg = 1<<16;
	for(i = 1; i < nelem(ctlr->rx); i++){
		ctlr->rx[i].regs = &ctlr->regs[RdmaOffset + nelem(ctlr->rd)*3 + (i-1)*RingCfg];
		ctlr->rx[i].intregs = &ctlr->regs[Intrl1];
		ctlr->rx[i].intmask = 0x10000 << (i - 1);
		ctlr->rx[i].num = i - 1;
		rcfg |= 1<<(i-1);
	}
	assert(rcfg && (rcfg & ~RxRingCfgMask) == 0);

	ctlr->tx->intregs = &ctlr->regs[Intrl0];
	ctlr->tx->intmask = IrqTxDmaDone;
	ctlr->tx->num = 16;
	tcfg = 1<<16;
	for(i = 1; i < nelem(ctlr->tx); i++){
		ctlr->tx[i].regs = &ctlr->regs[TdmaOffset + nelem(ctlr->td)*3 + (i-1)*RingCfg];
		ctlr->tx[i].intregs = &ctlr->regs[Intrl1];
		ctlr->tx[i].intmask = 1 << (i - 1);
		ctlr->tx[i].num = i - 1;
		tcfg |= 1<<(i-1);
	}
	assert(tcfg && (tcfg & ~TxRingCfgMask) == 0);

	REG(ctlr->rx->regs[DmaScbBurstSize]) = 0x08;
	for(i = 1; i < nelem(ctlr->rx); i++)
		initring(&ctlr->rx[i], ctlr->rd, (i-1)*32, 32);
	initring(ctlr->rx, ctlr->rd, (i-1)*32, nelem(ctlr->rd) - (i-1)*32);

	for(i = 0; i < nelem(ctlr->rx); i++){		 
		REG(ctlr->rx[i].regs[DmaDoneThresh]) = 1;
		REG(ctlr->rx[i].regs[RdmaXonXoffThresh]) = (5 << 16) | ((ctlr->rx[i].m+1) >> 4);

		// set dma timeout to 50µs
		REG(ctlr->rx->regs[RdmaTimeout0 + ctlr->rx[i].num]) = ((50*1000 + 8191)/8192);
	}

	REG(ctlr->tx->regs[DmaScbBurstSize]) = 0x08;
	for(i = 1; i < nelem(ctlr->tx); i++)
		initring(&ctlr->tx[i], ctlr->td, (i-1)*32, 32);
	initring(ctlr->tx, ctlr->td, (i-1)*32, nelem(ctlr->td) - (i-1)*32);

	dmapri[0] = dmapri[1] = dmapri[2] = 0;
	for(i = 0; i < nelem(ctlr->tx); i++){
		REG(ctlr->tx[i].regs[DmaDoneThresh]) = 10;
		REG(ctlr->tx[i].regs[TdmaFlowPeriod]) = i ? 0 : Maxtu << 16;
		dmapri[ctlr->tx[i].num/6] |= i << ((ctlr->tx[i].num%6)*5);
	}

	REG(ctlr->tx->regs[TdmaArbCtrl]) = 2;
	REG(ctlr->tx->regs[TdmaPriority0]) = dmapri[0];
	REG(ctlr->tx->regs[TdmaPriority1]) = dmapri[1];
	REG(ctlr->tx->regs[TdmaPriority2]) = dmapri[2];

	REG(ctlr->rx->regs[RingCfg]) = rcfg;
	REG(ctlr->tx->regs[RingCfg]) = tcfg;

	REG(ctlr->rx->regs[DmaCtrl]) |= rcfg<<1;
	REG(ctlr->tx->regs[DmaCtrl]) |= tcfg<<1;
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
	REG(ctlr->regs[UmacMac0]) = ea[0]<<24 | ea[1]<<16 | ea[2]<<8 | ea[3];
	REG(ctlr->regs[UmacMac1]) = ea[4]<<8 | ea[5];
}

static void
sethfb(Ctlr *ctlr)
{
	int i;

	REG(ctlr->regs[HfbCtlr]) = 0;
	REG(ctlr->regs[HfbFltEnable]) = 0;
	REG(ctlr->regs[HfbFltEnable+1]) = 0;

	for(i = 0; i < 8; i++)
		REG(ctlr->rx->regs[RdmaIndex2Ring0+i]) = 0;

	for(i = 0; i < 48/4; i++)
		REG(ctlr->regs[HfbFltLen + i]) = 0;

	for(i = 0; i < 48*128; i++)
		REG(ctlr->regs[HfbOffset + i]) = 0;
}

static int
mdiodone(void *arg)
{
	Ctlr *ctlr = arg;
	REG(ctlr->regs[Intrl0 + IntrMaskClr]) = (IrqMdioDone|IrqMdioError);
	return (REG(ctlr->regs[MdioCmd]) & MdioStartBusy) == 0;
}

static int
mdiowait(Ctlr *ctlr)
{
	REG(ctlr->regs[MdioCmd]) |= MdioStartBusy;
	while(REG(ctlr->regs[MdioCmd]) & MdioStartBusy)
		tsleep(ctlr->mii, mdiodone, ctlr, 10);
	return 0;
}

static int
mdiow(Mii* mii, int phy, int addr, int data)
{
	Ctlr *ctlr = mii->ctlr;

	if(phy > MdioPhyMask)
		return -1;
	addr &= MdioAddrMask;
	REG(ctlr->regs[MdioCmd]) = MdioWrite
		| (phy << MdioPhyShift) | (addr << MdioAddrShift) | (data & 0xFFFF);
	return mdiowait(ctlr);
}

static int
mdior(Mii* mii, int phy, int addr)
{
	Ctlr *ctlr = mii->ctlr;

	if(phy > MdioPhyMask)
		return -1;
	addr &= MdioAddrMask;
	REG(ctlr->regs[MdioCmd]) = MdioRead
		| (phy << MdioPhyShift) | (addr << MdioAddrShift);
	if(mdiowait(ctlr) < 0)
		return -1;
	if(REG(ctlr->regs[MdioCmd]) & MdioReadFail)
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
	REG(ctlr->regs[Intrl0 + IntrMaskClr]) = IrqLinkUp|IrqLinkDown;
	return 0;
}

static void
linkproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	MiiPhy *phy;
	int link = -1;

	while(waserror())
		;

	for(;;){
		tsleep(ctlr->link, linkevent, ctlr, 1000);
		miistatus(ctlr->mii);
		phy = ctlr->mii->curphy;
		if(phy == nil || phy->link == link)
			continue;
		link = phy->link;
		if(link){
			u32int cmd = CmdRxEn|CmdTxEn;
			switch(phy->speed){
			case 1000:	cmd |= CmdSpeed1000; break;
			case 100:	cmd |= CmdSpeed100; break;
			case 10:	cmd |= CmdSpeed10; break;
			}
			if(!phy->fd)
				cmd |= CmdHdEn;
			if(!phy->rfc)
				cmd |= CmdRxPauseIgn;
			if(!phy->tfc)
				cmd |= CmdTxPauseIgn;

			REG(ctlr->regs[ExtRgmiiOobCtrl]) = (REG(ctlr->regs[ExtRgmiiOobCtrl]) & ~OobDisable) | RgmiiLink;
			umaccmd(ctlr, cmd, CmdSpeedMask|CmdHdEn|CmdRxPauseIgn|CmdTxPauseIgn);

			edev->mbps = phy->speed;
		}
		edev->link = link;
		// print("#l%d: link %d speed %d\n", edev->ctlrno, edev->link, edev->mbps);
	}
}

static void
setmdfaddr(Ctlr *ctlr, int i, uchar *ea)
{
	REG(ctlr->regs[UmacMdfAddr0 + i*2 + 0]) = ea[0] << 8  | ea[1];
	REG(ctlr->regs[UmacMdfAddr0 + i*2 + 1]) = ea[2] << 24 | ea[3] << 16 | ea[4] << 8 | ea[5];
}

static void
rxmode(Ether *edev, int prom)
{
	Ctlr *ctlr = edev->ctlr;
	Netaddr *na;
	int i;

	if(prom || edev->nmaddr > 16-2){
		REG(ctlr->regs[UmacMdfCtrl]) = 0;
		umaccmd(ctlr, CmdProm, 0);
		return;
	}
	setmdfaddr(ctlr, 0, edev->bcast);
	setmdfaddr(ctlr, 1, edev->ea);
	for(i = 2, na = edev->maddr; na != nil; na = na->next, i++)
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
	if(ctlr->attached){
		qunlock(ctlr);
		return;
	}
	if(waserror()){
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
	REG(ctlr->regs[UmacMpdCtrl]) &= ~(MpdPwEn|MpdEn);

	// power
	REG(ctlr->regs[UmacEeeCtrl]) &= ~UmacEeeEn;
	REG(ctlr->regs[RbufEnergyCtrl]) &= ~(RbufEeeEn|RbufPmEn);
	REG(ctlr->regs[TbufEnergyCtrl]) &= ~(RbufEeeEn|RbufPmEn);
	REG(ctlr->regs[TbufBpMc]) = 0;

	REG(ctlr->regs[UmacMaxFrameLen]) = Maxtu;

	REG(ctlr->regs[RbufTbufSizeCtrl]) = 1;

	REG(ctlr->regs[TbufCtrl]) &= ~(Rbuf64En);
	REG(ctlr->regs[RbufCtrl]) &= ~(Rbuf64En|RbufAlign2B);
	REG(ctlr->regs[RbufChkCtrl]) &= ~(RbufChkRxChkEn|RbufChkSkipFcs);

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

	if(ctlr->mii->curphy == nil)
		error("no phy");

	print("#l%d: phy%d id %.8ux oui %x\n", 
		edev->ctlrno, ctlr->mii->curphy->phyno, 
		ctlr->mii->curphy->id, ctlr->mii->curphy->oui);

	miireset(ctlr->mii);

	switch(ctlr->mii->curphy->id){
	case 0x600d84a2:	/* BCM54312PE */
		/* mask interrupts */
		miimiw(ctlr->mii, 0x10, miimir(ctlr->mii, 0x10) | 0x1000);

		/* SCR3: clear DLLAPD_DIS */
		bcmshdw(ctlr->mii, 0x05, bcmshdr(ctlr->mii, 0x05) &~0x0002);
		/* APD: set APD_EN */
		bcmshdw(ctlr->mii, 0x0a, bcmshdr(ctlr->mii, 0x0a) | 0x0020);

		/* blinkenlights */
		bcmshdw(ctlr->mii, 0x09, bcmshdr(ctlr->mii, 0x09) | 0x0010);
		bcmshdw(ctlr->mii, 0x0d, 3<<0 | 0<<4);
		break;
	}

	/* don't advertise EEE */
	miimmdw(ctlr->mii, 7, 60, 0);

	miiane(ctlr->mii, ~0, AnaAP|AnaP, ~0);

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

	if(ctlr->regs != nil)
		return -1;

	ctlr->regs = (u32int*)(VIRTIO + 0x580000);
	ctlr->rx->regs = &ctlr->regs[RdmaOffset + nelem(ctlr->rd)*3 + 16*RingCfg];
	ctlr->tx->regs = &ctlr->regs[TdmaOffset + nelem(ctlr->td)*3 + 16*RingCfg];

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

	intrenable(edev->irq+0, interrupt0, edev, BUSUNKNOWN, edev->name);
	intrenable(edev->irq+1, interrupt1, edev, BUSUNKNOWN, edev->name);

	return 0;
}

void
ethergenetlink(void)
{
	addethercard("genet", pnp);
}
achedwbse(scratch->rp, Rbsz);
	}

	for(i = 0; i < nelem(ctlr->rd); i++){
		ctlr->rd[i].d = &ctlr->regs[RdmaOffset + i*3];
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
	for(phyno = 0; phyno < NMiiPhy; phyno++){
		bit = 1<<phyno;
		if(!(mask & bit))
			continue;
		if(mii->mask & bit){
			rmask |= bit;
			continue;
		}
		if(mii->mir(mii, phyno, Bmsr) == -1)
			continue;
		id = mii->mir(mii, phyno, Phyidr1) << 16;
		id |= mii->mir(mii, phyno, Phyidr2);
		oui = (id & 0x3FFFFC00)>>10;
		if(oui == 0xFFFFF || oui == 0)
			continue;

		if((miiphy = malloc(sizeof(MiiPhy))) == nil)
			continue;

		miiphy->mii = mii;
		miiphy->id = id;
		miiphy->oui = oui;
		miiphy->phyno = phyno;

		miiphy->anar = ~0;
		miiphy->fc = ~0;
		miiphy->mscr = ~0;

		mii->phy[phyno] = miiphy;
		if(mii->curphy == nil)
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
	if(mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	return mii->mir(mii, mii->curphy->phyno, r);
}

int
miimiw(Mii* mii, int r, int data)
{
	if(mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	return mii->miw(mii, mii->curphy->phyno, r, data);
}

int
miireset(Mii* mii)
{
	int bmcr;

	if(mii == nil || mii->ctlr == nil || mii->curphy == nil)
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

	if(mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	phyno = mii->curphy->phyno;

	bmsr = mii->mir(mii, phyno, Bmsr);
	if(!(bmsr & BmsrAna))
		return -1;

	if(a != ~0)
		anar = (AnaTXFD|AnaTXHD|Ana10FD|Ana10HD) & a;
	else if(mii->curphy->anar != ~0)
		anar = mii->curphy->anar;
	else{
		anar = mii->mir(mii, phyno, Anar);
		anar &= ~(AnaAP|AnaP|AnaT4|AnaTXFD|AnaTXHD|Ana10FD|Ana10HD);
		if(bmsr & Bmsr10THD)
			anar |= Ana10HD;
		if(bmsr & Bmsr10TFD)
			anar |= Ana10FD;
		if(bmsr & Bmsr100TXHD)
			anar |= AnaTXHD;
		if(bmsr & Bmsr100TXFD)
			anar |= AnaTXFD;
	}
	mii->curphy->anar = anar;

	if(p != ~0)
		anar |= (AnaAP|AnaP) & p;
	else if(mii->curphy->fc != ~0)
		anar |= mii->curphy->fc;
	mii->curphy->fc = (AnaAP|AnaP) & anar;

	if(bmsr & BmsrEs){
		mscr = mii->mir(mii, phyno, Mscr);
		mscr &= ~(Mscr1000TFD|Mscr1000THD);
		if(e != ~0)
			mscr |= (Mscr1000TFD|Mscr1000THD) & e;
		else if(mii->curphy->mscr != ~0)
			mscr = mii->curphy->mscr;
		else{
			r = mii->mir(mii, phyno, Esr);
			if(r & Esr1000THD)
				mscr |= Mscr1000THD;
			if(r & Esr1000TFD)
				mscr |= Mscr1000TFD;
		}
		mii->curphy->mscr = mscr;
		mii->miw(mii, phyno, Mscr, mscr);
	}
	mii->miw(mii, phyno, Anar, anar);

	r = mii->mir(mii, phyno, Bmcr);
	if(!(r & BmcrR)){
		r |= BmcrAne|BmcrRan;
		mii->miw(mii, phyno, Bmcr, r);
	}

	return 0;
}

int
miistatus(Mii* mii)
{
	MiiPhy *phy;
	int anlpar, bmsr, p, r, phyno;

	if(mii == nil || mii->ctlr == nil || mii->curphy == nil)
		return -1;
	phy = mii->curphy;
	phyno = phy->phyno;

	/*
	 * Check Auto-Negotiation is complete and link is up.
	 * (Read status twice as the Ls bit is sticky).
	 */
	bmsr = mii->mir(mii, phyno, Bmsr);
	if(!(bmsr & (BmsrAnc|BmsrAna))) {
		// print("miistatus: auto-neg incomplete\n");
		return -1;
	}

	bmsr = mii->mir(mii, phyno, Bmsr);
	if(!(bmsr & BmsrLs)){
		// print("miistatus: link down\n");
		phy->link = 0;
		return -1;
	}

	phy->speed = phy->fd = phy->rfc = phy->tfc = 0;
	if(phy->mscr){
		r = mii->mir(mii, phyno, Mssr);
		if((phy->mscr & Mscr1000TFD) && (r & Mssr1000TFD)){
			phy->speed = 1000;
			phy->fd = 1;
		}
		else if((phy->mscr & Mscr1000THD) && (r & Mssr1000THD))
			phy->speed = 1000;
	}

	anlpar = mii->mir(mii, phyno, Anlpar);
	if(phy->speed == 0){
		r = phy->anar & anlpar;
		if(r & AnaTXFD){
			phy->speed = 100;
			phy->fd = 1;
		}
		else if(r & AnaTXHD)
			phy->speed = 100;
		else if(r & Ana10FD){
			phy->speed = 10;
			phy->fd = 1;
		}
		else if(r & Ana10HD)
			phy->speed = 10;
	}
	if(phy->speed == 0) {
		// print("miistatus: phy speed 0\n");
		return -1;
	}

	if(phy->fd){
		p = phy->fc;
		r = anlpar & (AnaAP|AnaP);
		if(p == AnaAP && r == (AnaAP|AnaP))
			phy->tfc = 1;
		else if(p == (AnaAP|AnaP) && r == AnaAP)
			phy->rfc = 1;
		else if((p & AnaP) && (r & AnaP))
			phy->rfc = phy->tfc = 1;
	}

	phy->link = 1;

	return 0;
}

int
miimmdr(Mii* mii, int a, int r)
{
	a &= 0x1F;
	if(miimiw(mii, Mmdctrl, a) == -1)
		return -1;
	if(miimiw(mii, Mmddata, r) == -1)
		return -1;
	if(miimiw(mii, Mmdctrl, a | 0x4000) == -1)
		return -1;
	return miimir(mii, Mmddata);
}

int
miimmdw(Mii* mii, int a, int r, int data)
{
	a &= 0x1F;
	if(miimiw(mii, Mmdctrl, a) == -1)
		return -1;
	if(miimiw(mii, Mmddata, r) == -1)
		return -1;
	if(miimiw(mii, Mmdctrl, a | 0x4000) == -1)
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
	if(ctlr->attached){
		qunlock(ctlr);
		return;
	}
	if(waserror()){
		print("#l%d: %s\n", edev->ctlrno, up->errstr);
		shutdown(edev);
		freebufs(ctlr);
		qunlock(ctlr);
		nexterror();
	}

	// statistics
	REG(ctlr->regs[UmacMibCtrl]) = MibResetRxethermii.h                                                                                             644       0       0         6562 13530716010  11174                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    typedef struct Mii Mii;
typedef struct MiiPhy MiiPhy;

enum {					/* registers */
	Bmcr		= 0x00,		/* Basic Mode Control */
	Bmsr		= 0x01,		/* Basic Mode Status */
	Phyidr1		= 0x02,		/* PHY Identifier #1 */
	Phyidr2		= 0x03,		/* PHY Identifier #2 */
	Anar		= 0x04,		/* Auto-Negotiation Advertisement */
	Anlpar		= 0x05,		/* AN Link Partner Ability */
	Aner		= 0x06,		/* AN Expansion */
	Annptr		= 0x07,		/* AN Next Page TX */
	Annprr		= 0x08,		/* AN Next Page RX */
	Mscr		= 0x09,		/* MASTER-SLAVE Control */
	Mssr		= 0x0A,		/* MASTER-SLAVE Status */
	Mmdctrl		= 0x0D,		/* MMD Access Control */
	Mmddata		= 0x0E,		/* MMD Access Data Register */
	Esr		= 0x0F,		/* Extended Status */

	NMiiPhyr	= 32,
	NMiiPhy		= 32,
};

enum {					/* Bmcr */
	BmcrSs1		= 0x0040,	/* Speed Select[1] */
	BmcrCte		= 0x0080,	/* Collision Test Enable */
	BmcrDm		= 0x0100,	/* Duplex Mode */
	BmcrRan		= 0x0200,	/* Restart Auto-Negotiation */
	BmcrI		= 0x0400,	/* Isolate */
	BmcrPd		= 0x0800,	/* Power Down */
	BmcrAne		= 0x1000,	/* Auto-Negotiation Enable */
	BmcrSs0		= 0x2000,	/* Speed Select[0] */
	BmcrLe		= 0x4000,	/* Loopback Enable */
	BmcrR		= 0x8000,	/* Reset */
};

enum {					/* Bmsr */
	BmsrEc		= 0x0001,	/* Extended Capability */
	BmsrJd		= 0x0002,	/* Jabber Detect */
	BmsrLs		= 0x0004,	/* Link Status */
	BmsrAna		= 0x0008,	/* Auto-Negotiation Ability */
	BmsrRf		= 0x0010,	/* Remote Fault */
	BmsrAnc		= 0x0020,	/* Auto-Negotiation Complete */
	BmsrPs		= 0x0040,	/* Preamble Suppression Capable */
	BmsrEs		= 0x0100,	/* Extended Status */
	Bmsr100T2HD	= 0x0200,	/* 100BASE-T2 HD Capable */
	Bmsr100T2FD	= 0x0400,	/* 100BASE-T2 FD Capable */
	Bmsr10THD	= 0x0800,	/* 10BASE-T HD Capable */
	Bmsr10TFD	= 0x1000,	/* 10BASE-T FD Capable */
	Bmsr100TXHD	= 0x2000,	/* 100BASE-TX HD Capable */
	Bmsr100TXFD	= 0x4000,	/* 100BASE-TX FD Capable */
	Bmsr100T4	= 0x8000,	/* 100BASE-T4 Capable */
};

enum {					/* Anar/Anlpar */
	Ana10HD		= 0x0020,	/* Advertise 10BASE-T */
	Ana10FD		= 0x0040,	/* Advertise 10BASE-T FD */
	AnaTXHD		= 0x0080,	/* Advertise 100BASE-TX */
	AnaTXFD		= 0x0100,	/* Advertise 100BASE-TX FD */
	AnaT4		= 0x0200,	/* Advertise 100BASE-T4 */
	AnaP		= 0x0400,	/* Pause */
	AnaAP		= 0x0800,	/* Asymmetrical Pause */
	AnaRf		= 0x2000,	/* Remote Fault */
	AnaAck		= 0x4000,	/* Acknowledge */
	AnaNp		= 0x8000,	/* Next Page Indication */
};

enum {					/* Mscr */
	Mscr1000THD	= 0x0100,	/* Advertise 1000BASE-T HD */
	Mscr1000TFD	= 0x0200,	/* Advertise 1000BASE-T FD */
};

enum {					/* Mssr */
	Mssr1000THD	= 0x0400,	/* Link Partner 1000BASE-T HD able */
	Mssr1000TFD	= 0x0800,	/* Link Partner 1000BASE-T FD able */
};

enum {					/* Esr */
	Esr1000THD	= 0x1000,	/* 1000BASE-T HD Capable */
	Esr1000TFD	= 0x2000,	/* 1000BASE-T FD Capable */
	Esr1000XHD	= 0x4000,	/* 1000BASE-X HD Capable */
	Esr1000XFD	= 0x8000,	/* 1000BASE-X FD Capable */
};

typedef struct Mii {
	Lock;
	int	nphy;
	int	mask;
	MiiPhy*	phy[NMiiPhy];
	MiiPhy*	curphy;

	void*	ctlr;
	int	(*mir)(Mii*, int, int);
	int	(*miw)(Mii*, int, int, int);
} Mii;

typedef struct MiiPhy {
	Mii*	mii;
	u32int	id;
	int	oui;
	int	phyno;

	int	anar;
	int	fc;
	int	mscr;

	int	link;
	int	speed;
	int	fd;
	int	rfc;
	int	tfc;
};

extern int mii(Mii*, int);
extern int miiane(Mii*, int, int, int);
extern int miimir(Mii*, int);
extern int miimiw(Mii*, int, int);
extern int miireset(Mii*);
extern int miistatus(Mii*);

extern int miimmdr(Mii*, int, int);
extern int miimmdw(Mii*, int, int, int);
                                                                                                                                              etherusb.c                                                                                             664       0       0        21270 13514062573  11227                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
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

#define	GET4(p)		((p)[3]<<24 | (p)[2]<<16 | (p)[1]<<8  | (p)[0])
#define	PUT4(p, v)	((p)[0] = (v), (p)[1] = (v)>>8, \
			 (p)[2] = (v)>>16, (p)[3] = (v)>>24)
#define	dprint	if(debug) print
#define ddump	if(0) dump

static int debug = 0;

enum {
	Bind	= 0,
	Unbind,

	SmscRxerror	= 0x8000,
	SmscTxfirst	= 0x2000,
	SmscTxlast	= 0x1000,
	Lan78Rxerror = 0x00400000,
	Lan78Txfcs	= 1<<22,
};

typedef struct Ctlr Ctlr;
typedef struct Udev Udev;

typedef int (Unpackfn)(Ether*, Block*);
typedef void (Transmitfn)(Ctlr*, Block*);

struct Ctlr {
	Ether*	edev;
	Udev*	udev;
	Chan*	inchan;
	Chan*	outchan;
	char*	buf;
	int	bufsize;
	int	maxpkt;
	uint	rxbuf;
	uint	rxpkt;
	uint	txbuf;
	uint	txpkt;
	QLock;
};

struct Udev {
	char	*name;
	Unpackfn *unpack;
	Transmitfn *transmit;
};
	
static Cmdtab cmds[] = {
	{ Bind,		"bind",		7, },
	{ Unbind,	"unbind",	0, },
};

static Unpackfn unpackcdc, unpackasix, unpacksmsc, unpacklan78;
static Transmitfn transmitcdc, transmitasix, transmitsmsc, transmitlan78;

static Udev udevtab[] = {
	{ "cdc",	unpackcdc,	transmitcdc, },
	{ "asix",	unpackasix,	transmitasix, },
	{ "smsc",	unpacksmsc,	transmitsmsc, },
	{ "lan78xx",	unpacklan78, transmitlan78, },
	{ nil },
};

static char nullea[Eaddrlen];

static void
dump(int c, Block *b)
{
	int s, i;

	s = splhi();
	print("%c%ld:", c, BLEN(b));
	for(i = 0; i < 32; i++)
		print(" %2.2ux", b->rp[i]);
	print("\n");
	splx(s);
}

static int
unpack(Ether *edev, Block *b, int m)
{
	Block *nb;
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	ddump('?', b);
	if(m == BLEN(b)){
		etheriq(edev, b, 1);
		ctlr->rxpkt++;
		return 1;
	}
	nb = iallocb(m);
	if(nb != nil){
		memmove(nb->wp, b->rp, m);
		nb->wp += m;
		etheriq(edev, nb, 1);
		ctlr->rxpkt++;
	}else
		edev->soverflows++;
	b->rp += m;
	return 0;
}

static int
unpackcdc(Ether *edev, Block *b)
{
	int m;

	m = BLEN(b);
	if(m < 6)
		return -1;
	return unpack(edev, b, m);
}

static int
unpackasix(Ether *edev, Block *b)
{
	ulong hd;
	int m;
	uchar *wp;

	if(BLEN(b) < 4)
		return -1;
	hd = GET4(b->rp);
	b->rp += 4;
	m = hd & 0xFFFF;
	hd >>= 16;
	if(m != (~hd & 0xFFFF))
		return -1;
	m = ROUND(m, 2);
	if(m < 6 || m > BLEN(b))
		return -1;
	if((wp = b->rp + m) != b->wp && b->wp - wp < 4)
		b->wp = wp;
	return unpack(edev, b, m);
}

static int
unpacksmsc(Ether *edev, Block *b)
{
	ulong hd;
	int m;
	
	ddump('@', b);
	if(BLEN(b) < 4)
		return -1;
	hd = GET4(b->rp);
	b->rp += 4;
	m = hd >> 16;
	if(m < 6 || m > BLEN(b))
		return -1;
	if(BLEN(b) - m < 4)
		b->wp = b->rp + m;
	if(hd & SmscRxerror){
		edev->frames++;
		b->rp += m;
		if(BLEN(b) == 0){
			freeb(b);
			return 1;
		}
	}else if(unpack(edev, b, m) == 1)
		return 1;
	if((m &= 3) != 0)
		b->rp += 4 - m;
	return 0;
}

static int
unpacklan78(Ether *edev, Block *b)
{
	ulong hd;
	int m;

	if(BLEN(b) < 10)
		return -1;
	hd = GET4(b->rp);
	b->rp += 10;
	m = hd & 0x3FFF;
	if(m < 6 || m > BLEN(b))
		return -1;
	if(hd & Lan78Rxerror){
		edev->frames++;
		b->rp += m;
		if(BLEN(b) == 0){
			freeb(b);
			return 1;
		}
	}else if(unpack(edev, b, m) == 1)
		return 1;
	if(BLEN(b) > 0)
		b->rp = (uchar*)((((uintptr)b->rp)+3)&~3);
	return 0;
}

static void
transmit(Ctlr *ctlr, Block *b)
{
	Chan *c;

	ddump('!', b);
	c = ctlr->outchan;
	devtab[c->type]->bwrite(c, b, 0);
}

static void
transmitcdc(Ctlr *ctlr, Block *b)
{
	transmit(ctlr, b);
}

static void
transmitasix(Ctlr *ctlr, Block *b)
{
	int n;

	n = BLEN(b) & 0xFFFF;
	n |= ~n << 16;
	b = padblock(b, 4);
	PUT4(b->rp, n);
	if(BLEN(b) % ctlr->maxpkt == 0){
		b = padblock(b, -4);
		PUT4(b->wp, 0xFFFF0000);
		b->wp += 4;
	}
	transmit(ctlr, b);
}

static void
transmitsmsc(Ctlr *ctlr, Block *b)
{
	int n;

	n = BLEN(b) & 0x7FF;
	b = padblock(b, 8);
	PUT4(b->rp, n | SmscTxfirst | SmscTxlast);
	PUT4(b->rp+4, n);
	transmit(ctlr, b);
}

static void
transmitlan78(Ctlr *ctlr, Block *b)
{
	int n;

	n = BLEN(b) & 0xFFFFF;
	b = padblock(b, 8);
	PUT4(b->rp, n | Lan78Txfcs);
	PUT4(b->rp+4, n);
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
	if(waserror()){
		print("etherusbproc: error exit %s\n", up->errstr);
		pexit(up->errstr, 1);
		return;
	}
	for(;;){
		if(b == nil){
			b = devtab[c->type]->bread(c, ctlr->bufsize, 0);
			ctlr->rxbuf++;
		}
		switch(ctlr->udev->unpack(edev, b)){
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
bind(Ctlr *ctlr, Udev *udev, Cmdbuf *cb)
{
	Chan *inchan, *outchan;
	char *buf;
	uint bufsize, maxpkt;
	uchar ea[Eaddrlen];

	qlock(ctlr);
	inchan = outchan = nil;
	buf = nil;
	if(waserror()){
		free(buf);
		if(inchan)
			cclose(inchan);
		if(outchan)
			cclose(outchan);
		qunlock(ctlr);
		nexterror();
	}
	if(ctlr->buf != nil)
		cmderror(cb, "already bound to a device");
	maxpkt = strtol(cb->f[6], 0, 0);
	if(maxpkt < 8 || maxpkt > 512)
		cmderror(cb, "bad maxpkt");
	bufsize = strtol(cb->f[5], 0, 0);
	if(bufsize < maxpkt || bufsize > 32*1024)
		cmderror(cb, "bad bufsize");
	buf = smalloc(bufsize);
	inchan = namec(cb->f[2], Aopen, OREAD, 0);
	outchan = namec(cb->f[3], Aopen, OWRITE, 0);
	assert(inchan != nil && outchan != nil);
	if(parsemac(ea, cb->f[4], Eaddrlen) != Eaddrlen)
		cmderror(cb, "bad etheraddr");
	if(memcmp(ctlr->edev->ea, nullea, Eaddrlen) == 0)
		memmove(ctlr->edev->ea, ea, Eaddrlen);
	else if(memcmp(ctlr->edev->ea, ea, Eaddrlen) != 0)
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
unbind(Ctlr *ctlr)
{
	qlock(ctlr);
	if(ctlr->buf != nil){
		free(ctlr->buf);
		ctlr->buf = nil;
		if(ctlr->inchan)
			cclose(ctlr->inchan);
		if(ctlr->outchan)
			cclose(ctlr->outchan);
		ctlr->inchan = ctlr->outchan = nil;
	}
	qunlock(ctlr);
}

static long
etherusbifstat(Ether* edev, void* a, long n, ulong offset)
{
	Ctlr *ctlr;
	char *p;
	int l;

	ctlr = edev->ctlr;
	p = malloc(READSTR);
	l = 0;

	l += snprint(p+l, READSTR-l, "rxbuf: %ud\n", ctlr->rxbuf);
	l += snprint(p+l, READSTR-l, "rxpkt: %ud\n", ctlr->rxpkt);
	l += snprint(p+l, READSTR-l, "txbuf: %ud\n", ctlr->txbuf);
	l += snprint(p+l, READSTR-l, "txpkt: %ud\n", ctlr->txpkt);
	USED(l);

	n = readstr(offset, a, n, p);
	free(p);
	return n;
}

static void
etherusbtransmit(Ether *edev)
{
	Ctlr *ctlr;
	Block *b;
	
	ctlr = edev->ctlr;
	while((b = qget(edev->oq)) != nil){
		ctlr->txpkt++;
		if(ctlr->buf == nil)
			freeb(b);
		else{
			ctlr->udev->transmit(ctlr, b);
			ctlr->txbuf++;
		}
	}
}

static long
etherusbctl(Ether* edev, void* buf, long n)
{
	Ctlr *ctlr;
	Cmdbuf *cb;
	Cmdtab *ct;
	Udev *udev;

	if((ctlr = edev->ctlr) == nil)
		error(Enonexist);

	cb = parsecmd(buf, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, cmds, nelem(cmds));
	switch(ct->index){
	case Bind:
		for(udev = udevtab; udev->name; udev++)
			if(strcmp(cb->f[1], udev->name) == 0)
				break;
		if(udev->name == nil)
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
etherusbmulticast(void*, uchar*, int)
{
	/* nothing to do, we allow all multicast packets in */
}

static void
etherusbshutdown(Ether*)
{
}

static void
etherusbattach(Ether* edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if(ctlr->edev == 0){
		/*
		 * Don't let boot process access etherusb until
		 * usbether driver has assigned an address.
		 */
		if(up->pid == 1 && strcmp(up->text, "boot") == 0)
			while(memcmp(edev->ea, nullea, Eaddrlen) == 0)
				tsleep(&up->sleep, return0, 0, 100);
		ctlr->edev = edev;
	}
}

static int
etherusbpnp(Ether* edev)
{
	Ctlr *ctlr;

	ctlr = malloc(sizeof(Ctlr));
	edev->ctlr = ctlr;
	edev->irq = -1;
	edev->mbps = 100;	/* TODO: get this from usbether */

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
1000XHD	= 0x4000,	/* 1000BASE-X HD Capable */
	Esr1000XFD	= 0x8000,	/* 1000BASE-X FD Capable */
};

typedef struct Mii {
	Lock;
	int	nphy;
	int	mask;
	MiiPhy*	phy[NMiiPhy];
	MiiPhy*	curphy;

	void*	ctlr;
	int	(*mir)(Mii*, int, int);
	int	(*miw)(Mii*, int, int, int);
} Mii;

typedef struct MiiPhy {
	Mii*	mii;
	u32int	id;
	int	ofns.h                                                                                                  664       0       0        12503 13252727135  10201                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../port/portfns.h"

Dirtab*	addarchfile(char*, int, long(*)(Chan*, void*, long, vlong), 
	long(*)(Chan*, void*, long, vlong));
extern void archreboot(void);
extern void archreset(void);
extern void armtimerset(int);
extern void cachedwb(void);
extern void cachedwbinv(void);
extern void cachedinvse(void*, int);
extern void cachedwbse(void*, int);
extern void cachedwbinvse(void*, int);
extern void cachedwbtlb(void*, int);
extern void cacheiinv(void);
extern void cacheiinvse(void*, int);
extern void cacheuwbinv(void);
extern uintptr cankaddr(uintptr pa);
extern int cas32(void*, u32int, u32int);
extern int cas(ulong*, ulong, ulong);
extern void checkmmu(uintptr, uintptr);
extern void clockinit(void);
extern void clockshutdown(void);
extern int cmpswap(long*, long, long);
extern void coherence(void);
extern u32int cpidget(void);
extern ulong cprd(int cp, int op1, int crn, int crm, int op2);
extern ulong cprdsc(int op1, int crn, int crm, int op2);
extern void cpuidprint(void);
extern char *cputype2name(char *buf, int size);
extern void cpwr(int cp, int op1, int crn, int crm, int op2, ulong val);
extern void cpwrsc(int op1, int crn, int crm, int op2, ulong val);
#define cycles(ip) *(ip) = lcycles()
extern uintptr dmaaddr(void *va);
extern void dmastart(int, int, int, void*, void*, int);
extern int dmawait(int);
extern int fbblank(int);
extern void* fbinit(int, int*, int*, int*);
extern u32int farget(void);
extern void fpon(void);
extern ulong fprd(int fpreg);
extern void fprestreg(int fpreg, uvlong val);
extern void fpsave(FPsave *);
extern ulong fpsavereg(int fpreg, uvlong *fpp);
extern void fpwr(int fpreg, ulong val);
extern u32int fsrget(void);
extern uint getboardrev(void);
extern ulong getclkrate(int);
extern char* getconf(char*);
extern uint getcputemp(void);
extern char *getethermac(void);
extern uint getfirmware(void);
extern int getncpus(void);
extern int getpower(int);
extern void getramsize(Confmem*);
extern void gpiosel(uint, int);
extern void gpiopullup(uint);
extern void gpiopulloff(uint);
extern void gpiopulldown(uint);
extern void gpioout(uint, int);
extern int gpioin(unit);
extern void i2csetup(int);
extern long i2crecv(I2Cdev*, void*, long, ulong);
extern long i2csend(I2Cdev*, void*, long, ulong);
extern u32int ifsrget(void);
extern void irqenable(int, void (*)(Ureg*, void*), void*);
#define intrenable(i, f, a, b, n) irqenable((i), (f), (a))
extern void intrcpushutdown(void);
extern void intrshutdown(void);
extern void intrsoff(void);
extern int isaconfig(char*, int, ISAConf*);
extern int l2ap(int);
extern void l2cacheuwbinv(void);
extern void links(void);
extern void mmuinit(void*);
extern void mmuinit1(void);
extern void mmuinvalidate(void);
extern void mmuinvalidateaddr(u32int);
extern uintptr mmukmap(uintptr, uintptr, usize);
extern void okay(int);
extern void procrestore(Proc *);
extern void procsave(Proc*);
extern void procsetup(Proc*);
extern void screeninit(void);
#define sdfree(p) free(p)
#define sdmalloc(n)	mallocalign(n, BLOCKALIGN, 0, 0)
extern void setclkrate(int, ulong);
extern void setpower(int, int);
extern void setr13(int, u32int*);
extern void sev(void);
extern void spiclock(uint);
extern void spimode(int);
extern void spirw(uint, void*, int);
extern int splfhi(void);
extern int splflo(void);
extern void swcursorinit(void);
extern void syscallfmt(int syscallno, ulong pc, va_list list);
extern void sysretfmt(int syscallno, va_list list, long ret, uvlong start, uvlong stop);
extern int startcpus(uint);
extern void stopcpu(uint);
extern int tas(void *);
extern void touser(uintptr);
extern void trapinit(void);
extern void uartconsinit(void);
extern int userureg(Ureg*);
extern void vectors(void);
extern void vgpinit(void);
extern void vgpset(uint, int);
extern void vtable(void);
extern void wdogoff(void);
extern void wdogfeed(void);

/*
 * floating point emulation
 */
extern int fpiarm(Ureg*);
extern int fpudevprocio(Proc*, void*, long, uintptr, int);
extern void fpuinit(void);
extern void fpunoted(void);
extern void fpunotify(Ureg*);
extern void fpuprocrestore(Proc*);
extern void fpuprocsave(Proc*);
extern void fpusysprocsetup(Proc*);
extern void fpusysrfork(Ureg*);
extern void fpusysrforkchild(Proc*, Ureg*, Proc*);
extern int fpuemu(Ureg*);
/*
 * Miscellaneous machine dependent stuff.
 */
extern char* getenv(char*, char*, int);
uintptr mmukmap(uintptr, uintptr, usize);
uintptr mmukunmap(uintptr, uintptr, usize);
extern void* mmuuncache(void*, usize);
extern void* ucalloc(usize);
extern Block* ucallocb(int);
extern void* ucallocalign(usize size, int align, int span);
extern void ucfree(void*);
extern void ucfreeb(Block*);
/*
 * Things called from port.
 */
extern void delay(int);				/* only scheddump() */
extern int islo(void);
extern void microdelay(int);			/* only edf.c */
extern void idlehands(void);
extern void setkernur(Ureg*, Proc*);		/* only devproc.c */
extern void* sysexecregs(uintptr, ulong, int);
extern void sysprocsetup(Proc*);
extern void validalign(uintptr, unsigned);

extern void kexit(Ureg*);

#define	getpgcolor(a)	0
#define	kmapinval()
#define countpagerefs(a, b)

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define KADDR(pa)	UINT2PTR(KZERO    | ((uintptr)(pa) & ~KSEGM))
#define PADDR(va)	PTR2UINT(PHYSDRAM | ((uintptr)(va) & ~KSEGM))

#define MASK(v)	((1UL << (v)) - 1)	/* mask `v' bits wide */
f = nil;
	if(waserror()){
		free(buf);
		if(inchan)
			cclose(inchan);
		if(outchan)
			cclose(outchan);
		qunlock(ctlr);
		nexterror();
	}
	if(ctlr->buf != nil)
		cmderror(cb, "already boufpiarm.c                                                                                               664       0       0        23224 12670306631  10663                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * this doesn't attempt to implement ARM floating-point properties
 * that aren't visible in the Inferno environment.
 * all arithmetic is done in double precision.
 * the FP trap status isn't updated.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#include	"ureg.h"

#include	"arm.h"
#include	"../port/fpi.h"

#define ARM7500			/* emulate old pre-VFP opcodes */

/* undef this if correct kernel r13 isn't in Ureg;
 * check calculation in fpiarm below
 */

#define	REG(ur, x) (*(long*)(((char*)(ur))+roff[(x)]))
#ifdef ARM7500
#define	FR(ufp, x) (*(Internal*)(ufp)->regs[(x)&7])
#else
#define	FR(ufp, x) (*(Internal*)(ufp)->regs[(x)&(Nfpregs - 1)])
#endif

typedef struct FP2 FP2;
typedef struct FP1 FP1;

struct FP2 {
	char*	name;
	void	(*f)(Internal, Internal, Internal*);
};

struct FP1 {
	char*	name;
	void	(*f)(Internal*, Internal*);
};

enum {
	N = 1<<31,
	Z = 1<<30,
	C = 1<<29,
	V = 1<<28,
	REGPC = 15,
};

enum {
	fpemudebug = 0,
};

#undef OFR
#define	OFR(X)	((ulong)&((Ureg*)0)->X)

static	int	roff[] = {
	OFR(r0), OFR(r1), OFR(r2), OFR(r3),
	OFR(r4), OFR(r5), OFR(r6), OFR(r7),
	OFR(r8), OFR(r9), OFR(r10), OFR(r11),
	OFR(r12), OFR(r13), OFR(r14), OFR(pc),
};

static Internal fpconst[8] = {		/* indexed by op&7 (ARM 7500 FPA) */
	/* s, e, l, h */
	{0, 0x1, 0x00000000, 0x00000000}, /* 0.0 */
	{0, 0x3FF, 0x00000000, 0x08000000},	/* 1.0 */
	{0, 0x400, 0x00000000, 0x08000000},	/* 2.0 */
	{0, 0x400, 0x00000000, 0x0C000000},	/* 3.0 */
	{0, 0x401, 0x00000000, 0x08000000},	/* 4.0 */
	{0, 0x401, 0x00000000, 0x0A000000},	/* 5.0 */
	{0, 0x3FE, 0x00000000, 0x08000000},	/* 0.5 */
	{0, 0x402, 0x00000000, 0x0A000000},	/* 10.0 */
};

/*
 * arm binary operations
 */

static void
fadd(Internal m, Internal n, Internal *d)
{
	(m.s == n.s? fpiadd: fpisub)(&m, &n, d);
}

static void
fsub(Internal m, Internal n, Internal *d)
{
	m.s ^= 1;
	(m.s == n.s? fpiadd: fpisub)(&m, &n, d);
}

static void
fsubr(Internal m, Internal n, Internal *d)
{
	n.s ^= 1;
	(n.s == m.s? fpiadd: fpisub)(&n, &m, d);
}

static void
fmul(Internal m, Internal n, Internal *d)
{
	fpimul(&m, &n, d);
}

static void
fdiv(Internal m, Internal n, Internal *d)
{
	fpidiv(&m, &n, d);
}

static void
fdivr(Internal m, Internal n, Internal *d)
{
	fpidiv(&n, &m, d);
}

/*
 * arm unary operations
 */

static void
fmov(Internal *m, Internal *d)
{
	*d = *m;
}

static void
fmovn(Internal *m, Internal *d)
{
	*d = *m;
	d->s ^= 1;
}

static void
fabsf(Internal *m, Internal *d)
{
	*d = *m;
	d->s = 0;
}

static void
frnd(Internal *m, Internal *d)
{
	short e;

	(m->s? fsub: fadd)(fpconst[6], *m, d);
	if(IsWeird(d))
		return;
	fpiround(d);
	e = (d->e - ExpBias) + 1;
	if(e <= 0)
		SetZero(d);
	else if(e > FractBits){
		if(e < 2*FractBits)
			d->l &= ~((1<<(2*FractBits - e))-1);
	}else{
		d->l = 0;
		if(e < FractBits)
			d->h &= ~((1<<(FractBits-e))-1);
	}
}

/*
 * ARM 7500 FPA opcodes
 */

static	FP1	optab1[16] = {	/* Fd := OP Fm */
[0]	{"MOVF",	fmov},
[1]	{"NEGF",	fmovn},
[2]	{"ABSF",	fabsf},
[3]	{"RNDF",	frnd},
[4]	{"SQTF",	/*fsqt*/0},
/* LOG, LGN, EXP, SIN, COS, TAN, ASN, ACS, ATN all `deprecated' */
/* URD and NRM aren't implemented */
};

static	FP2	optab2[16] = {	/* Fd := Fn OP Fm */
[0]	{"ADDF",	fadd},
[1]	{"MULF",	fmul},
[2]	{"SUBF",	fsub},
[3]	{"RSUBF",	fsubr},
[4]	{"DIVF",	fdiv},
[5]	{"RDIVF",	fdivr},
/* POW, RPW deprecated */
[8]	{"REMF",	/*frem*/0},
[9]	{"FMF",	fmul},	/* fast multiply */
[10]	{"FDV",	fdiv},	/* fast divide */
[11]	{"FRD",	fdivr},	/* fast reverse divide */
/* POL deprecated */
};

static ulong
fcmp(Internal *n, Internal *m)
{
	int i;
	Internal rm, rn;

	if(IsWeird(m) || IsWeird(n)){
		/* BUG: should trap if not masked */
		return V|C;
	}
	rn = *n;
	rm = *m;
	fpiround(&rn);
	fpiround(&rm);
	i = fpicmp(&rn, &rm);
	if(i > 0)
		return C;
	else if(i == 0)
		return C|Z;
	else
		return N;
}

static void
fld(void (*f)(Internal*, void*), int d, ulong ea, int n, FPsave *ufp)
{
	void *mem;

	validaddr(ea, n, 0);
	mem = (void*)ea;
	(*f)(&FR(ufp, d), mem);
	if(fpemudebug)
		print("MOV%c #%lux, F%d\n", n==8? 'D': 'F', ea, d);
}

static void
fst(void (*f)(void*, Internal*), ulong ea, int s, int n, FPsave *ufp)
{
	Internal tmp;
	void *mem;

	validaddr(ea, n, 1);
	mem = (void*)ea;
	tmp = FR(ufp, s);
	if(fpemudebug)
		print("MOV%c	F%d,#%lux\n", n==8? 'D': 'F', s, ea);
	(*f)(mem, &tmp);
}

static int
condok(int cc, int c)
{
	switch(c){
	case 0:	/* Z set */
		return cc&Z;
	case 1:	/* Z clear */
		return (cc&Z) == 0;
	case 2:	/* C set */
		return cc&C;
	case 3:	/* C clear */
		return (cc&C) == 0;
	case 4:	/* N set */
		return cc&N;
	case 5:	/* N clear */
		return (cc&N) == 0;
	case 6:	/* V set */
		return cc&V;
	case 7:	/* V clear */
		return (cc&V) == 0;
	case 8:	/* C set and Z clear */
		return cc&C && (cc&Z) == 0;
	case 9:	/* C clear or Z set */
		return (cc&C) == 0 || cc&Z;
	case 10:	/* N set and V set, or N clear and V clear */
		return (~cc&(N|V))==0 || (cc&(N|V)) == 0;
	case 11:	/* N set and V clear, or N clear and V set */
		return (cc&(N|V))==N || (cc&(N|V))==V;
	case 12:	/* Z clear, and either N set and V set or N clear and V clear */
		return (cc&Z) == 0 && ((~cc&(N|V))==0 || (cc&(N|V))==0);
	case 13:	/* Z set, or N set and V clear or N clear and V set */
		return (cc&Z) || (cc&(N|V))==N || (cc&(N|V))==V;
	case 14:	/* always */
		return 1;
	case 15:	/* never (reserved) */
		return 0;
	}
	return 0;	/* not reached */
}

static void
unimp(ulong pc, ulong op)
{
	char buf[60];

	snprint(buf, sizeof(buf), "sys: fp: pc=%lux unimp fp 0x%.8lux", pc, op);
	if(fpemudebug)
		print("FPE: %s\n", buf);
	error(buf);
	/* no return */
}

static void
fpemu(ulong pc, ulong op, Ureg *ur, FPsave *ufp)
{
	int rn, rd, tag, o;
	long off;
	ulong ea;
	Internal tmp, *fm, *fn;

	/* note: would update fault status here if we noted numeric exceptions */

	/*
	 * LDF, STF; 10.1.1
	 */
	if(((op>>25)&7) == 6){
		if(op & (1<<22))
			unimp(pc, op);	/* packed or extended */
		rn = (op>>16)&0xF;
		off = (op&0xFF)<<2;
		if((op & (1<<23)) == 0)
			off = -off;
		ea = REG(ur, rn);
		if(rn == REGPC)
			ea += 8;
		if(op & (1<<24))
			ea += off;
		rd = (op>>12)&7;
		if(op & (1<<20)){
			if(op & (1<<15))
				fld(fpid2i, rd, ea, 8, ufp);
			else
				fld(fpis2i, rd, ea, 4, ufp);
		}else{
			if(op & (1<<15))
				fst(fpii2d, ea, rd, 8, ufp);
			else
				fst(fpii2s, ea, rd, 4, ufp);
		}
		if((op & (1<<24)) == 0)
			ea += off;
		if(op & (1<<21))
			REG(ur, rn) = ea;
		return;
	}

	/*
	 * CPRT/transfer, 10.3
	 */
	if(op & (1<<4)){
		rd = (op>>12) & 0xF;

		/*
		 * compare, 10.3.1
		 */
		if(rd == 15 && op & (1<<20)){
			rn = (op>>16)&7;
			fn = &FR(ufp, rn);
			if(op & (1<<3)){
				fm = &fpconst[op&7];
				if(fpemudebug)
					tag = 'C';
			}else{
				fm = &FR(ufp, op&7);
				if(fpemudebug)
					tag = 'F';
			}
			switch((op>>21)&7){
			default:
				unimp(pc, op);
			case 4:	/* CMF: Fn :: Fm */
			case 6:	/* CMFE: Fn :: Fm (with exception) */
				ur->psr &= ~(N|C|Z|V);
				ur->psr |= fcmp(fn, fm);
				break;
			case 5:	/* CNF: Fn :: -Fm */
			case 7:	/* CNFE: Fn :: -Fm (with exception) */
				tmp = *fm;
				tmp.s ^= 1;
				ur->psr &= ~(N|C|Z|V);
				ur->psr |= fcmp(fn, &tmp);
				break;
			}
			if(fpemudebug)
				print("CMPF	%c%d,F%ld =%#lux\n",
					tag, rn, op&7, ur->psr>>28);
			return;
		}

		/*
		 * other transfer, 10.3
		 */
		switch((op>>20)&0xF){
		default:
			unimp(pc, op);
		case 0:	/* FLT */
			rn = (op>>16) & 7;
			fpiw2i(&FR(ufp, rn), &REG(ur, rd));
			if(fpemudebug)
				print("MOVW[FD]	R%d, F%d\n", rd, rn);
			break;
		case 1:	/* FIX */
			if(op & (1<<3))
				unimp(pc, op);
			rn = op & 7;
			tmp = FR(ufp, rn);
			fpii2w(&REG(ur, rd), &tmp);
			if(fpemudebug)
				print("MOV[FD]W	F%d, R%d =%ld\n", rn, rd, REG(ur, rd));
			break;
		case 2:	/* FPSR := Rd */
			ufp->status = REG(ur, rd);
			if(fpemudebug)
				print("MOVW	R%d, FPSR\n", rd);
			break;
		case 3:	/* Rd := FPSR */
			REG(ur, rd) = ufp->status;
			if(fpemudebug)
				print("MOVW	FPSR, R%d\n", rd);
			break;
		case 4:	/* FPCR := Rd */
			ufp->control = REG(ur, rd);
			if(fpemudebug)
				print("MOVW	R%d, FPCR\n", rd);
			break;
		case 5:	/* Rd := FPCR */
			REG(ur, rd) = ufp->control;
			if(fpemudebug)
				print("MOVW	FPCR, R%d\n", rd);
			break;
		}
		return;
	}

	/*
	 * arithmetic
	 */

	if(op & (1<<3)){	/* constant */
		fm = &fpconst[op&7];
		if(fpemudebug)
			tag = 'C';
	}else{
		fm = &FR(ufp, op&7);
		if(fpemudebug)
			tag = 'F';
	}
	rd = (op>>12)&7;
	o = (op>>20)&0xF;
	if(op & (1<<15)){	/* monadic */
		FP1 *fp;
		fp = &optab1[o];
		if(fp->f == nil)
			unimp(pc, op);
		if(fpemudebug)
			print("%s	%c%ld,F%d\n", fp->name, tag, op&7, rd);
		(*fp->f)(fm, &FR(ufp, rd));
	} else {
		FP2 *fp;
		fp = &optab2[o];
		if(fp->f == nil)
			unimp(pc, op);
		rn = (op>>16)&7;
		if(fpemudebug)
			print("%s	%c%ld,F%d,F%d\n", fp->name, tag, op&7, rn, rd);
		(*fp->f)(*fm, FR(ufp, rn), &FR(ufp, rd));
	}
}

/*
 * returns the number of FP instructions emulated
 */
int
fpiarm(Ureg *ur)
{
	ulong op, o, cp;
	FPsave *ufp;
	int n;

	if(up == nil)
		panic("fpiarm not in a process");
	ufp = &up->fpsave;
	/*
	 * because all the emulated fp state is in the proc structure,
	 * it need not be saved/restored
	 */
	switch(up->fpstate){
	case FPactive:
	case FPinactive:
		error("illegal instruction: emulated fpu opcode in VFP mode");
	case FPinit:
		assert(sizeof(Internal) <= sizeof(ufp->regs[0]));
		up->fpstate = FPemu;
		ufp->control = 0;
		ufp->status = (0x01<<28)|(1<<12); /* sw emulation, alt. C flag */
		for(n = 0; n < 8; n++)
			FR(ufp, n) = fpconst[0];
	}
	for(n=0; ;n++){
		validaddr(ur->pc, 4, 0);
		op = *(ulong*)(ur->pc);
		if(fpemudebug)
			print("%#lux: %#8.8lux ", ur->pc, op);
		o = (op>>24) & 0xF;
		cp = (op>>8) & 0xF;
		if(!ISFPAOP(cp, o))
			break;
		if(condok(ur->psr, op>>28))
			fpemu(ur->pc, op, ur, ufp);
		ur->pc += 4;		/* pretend cpu executed the instr */
	}
	if(fpemudebug)
		print("\n");
	return n;
}
 10663                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    gpio.c                                                                                                 664       0       0         3163 13526512602  10321                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Raspberry Pi GPIO support
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define GPIOREGS	(VIRTIO+0x200000)

/* GPIO regs */
enum {
	Fsel0	= 0x00>>2,
		FuncMask= 0x7,
	Set0	= 0x1c>>2,
	Clr0	= 0x28>>2,
	Lev0	= 0x34>>2,
	PUD	= 0x94>>2,
		Off	= 0x0,
		Pulldown= 0x1,
		Pullup	= 0x2,
	PUDclk0	= 0x98>>2,
	PUDclk1	= 0x9c>>2,
	/* BCM2711 only */
	PUPPDN0	= 0xe4>>2,
	PUPPDN1	= 0xe8>>2,
	PUPPDN2	= 0xec>>2,
	PUPPDN3	= 0xf0>>2,
};

void
gpiosel(uint pin, int func)
{	
	u32int *gp, *fsel;
	int off;

	gp = (u32int*)GPIOREGS;
	fsel = &gp[Fsel0 + pin/10];
	off = (pin % 10) * 3;
	*fsel = (*fsel & ~(FuncMask<<off)) | func<<off;
}

static void
gpiopull(uint pin, int func)
{
	u32int *gp, *reg;
	u32int mask;
	int shift;
	static uchar map[4] = {0x00,0x02,0x01,0x00};

	gp = (u32int*)GPIOREGS;
	if(gp[PUPPDN3] == 0x6770696f){
		/* BCM2835, BCM2836, BCM2837 */
		reg = &gp[PUDclk0 + pin/32];
		mask = 1 << (pin % 32);
		gp[PUD] = func;
		microdelay(1);
		*reg = mask;
		microdelay(1);
		*reg = 0;
	} else {
		/* BCM2711 */
		reg = &gp[PUPPDN0 + pin/16];
		shift = 2*(pin % 16);
		*reg = (map[func] << shift) | (*reg & ~(3<<shift));
	}

}

void
gpiopulloff(uint pin)
{
	gpiopull(pin, Off);
}

void
gpiopullup(uint pin)
{
	gpiopull(pin, Pullup);
}

void
gpiopulldown(uint pin)
{
	gpiopull(pin, Pulldown);
}

void
gpioout(uint pin, int set)
{
	u32int *gp;
	int v;

	gp = (u32int*)GPIOREGS;
	v = set? Set0 : Clr0;
	gp[v + pin/32] = 1 << (pin % 32);
}

int
gpioin(uint pin)
{
	u32int *gp;

	gp = (u32int*)GPIOREGS;
	return (gp[Lev0 + pin/32] & (1 << (pin % 32))) != 0;
}

nal n, Internal *d)
{
	fpidiv(&m, &n, d);
}

static void
fdivr(Internal m, Internal n, Internal *d)
{
	fpidiv(&n, &m, d);
}

/*
 * arm unary operations
 */

static void
fmov(Internal *m, Internal *d)
{
	*d = *m;
}

static void
fmovn(Internal *m, Internal *d)
{
	*d = *m;
	d->s ^= 1;
}

static void
fabsf(Internal *m, Internal *d)
{
	*d = *m;
	d->s = 0;
}

static void
frnd(Internal *m, Internal *di2c.c                                                                                                  664       0       0        11246 12677476334  10102                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2835 i2c controller
 *
 *	Only i2c1 is supported.
 *	i2c2 is reserved for HDMI.
 *	i2c0 SDA0/SCL0 pins are not routed to P1 connector (except for early Rev 0 boards)
 *
 * maybe hardware problems lurking, see: https://github.com/raspberrypi/linux/issues/254
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#define I2CREGS	(VIRTIO+0x804000)
#define SDA0Pin	2
#define	SCL0Pin	3
#define	Alt0	0x4

typedef struct I2c I2c;
typedef struct Bsc Bsc;

/*
 * Registers for Broadcom Serial Controller (i2c compatible)
 */
struct Bsc {
	u32int	ctrl;
	u32int	stat;
	u32int	dlen;
	u32int	addr;
	u32int	fifo;
	u32int	clkdiv;		/* default 1500 => 100 KHz assuming 150Mhz input clock */
	u32int	delay;		/* default (48<<16)|48 falling:rising edge */
	u32int	clktimeout;	/* default 64 */
};

/*
 * Per-controller info
 */
struct I2c {
	QLock	lock;
	Lock	reglock;
	Rendez	r;
	Bsc	*regs;
};

static I2c i2c;

enum {
	/* ctrl */
	I2cen	= 1<<15,	/* I2c enable */
	Intr	= 1<<10,	/* interrupt on reception */
	Intt	= 1<<9,		/* interrupt on transmission */
	Intd	= 1<<8,		/* interrupt on done */
	Start	= 1<<7,		/* aka ST, start a transfer */
	Clear	= 1<<4,		/* clear fifo */
	Read	= 1<<0,		/* read transfer */
	Write	= 0<<0,		/* write transfer */

	/* stat */
	Clkt	= 1<<9,		/* clock stretch timeout */
	Err	= 1<<8,			/* NAK */
	Rxf	= 1<<7,			/* RX fifo full */
	Txe	= 1<<6,			/* TX fifo full */
	Rxd	= 1<<5,			/* RX fifo has data */
	Txd	= 1<<4,			/* TX fifo has space */
	Rxr	= 1<<3,			/* RX fiio needs reading */
	Txw	= 1<<2,			/* TX fifo needs writing */
	Done	= 1<<1,		/* transfer done */
	Ta	= 1<<0,			/* Transfer active */

	/* maximum I2C I/O (can change) */
	MaxIO =	128,
	MaxSA =	2,	/* longest subaddress */
	Bufsize = (MaxIO+MaxSA+1+4)&~3,		/* extra space for subaddress/clock bytes and alignment */

	Chatty = 0,
};

static void
i2cinterrupt(Ureg*, void*)
{
	Bsc *r;
	int st;

	r = i2c.regs;
	st = 0;
	if((r->ctrl & Intr) && (r->stat & Rxd))
		st |= Intr;
	if((r->ctrl & Intt) && (r->stat & Txd))
		st |= Intt;
	if(r->stat & Done)
		st |= Intd;
	if(st){
		r->ctrl &= ~st;
		wakeup(&i2c.r);
	}
}

static int
i2cready(void *st)
{
	return (i2c.regs->stat & (uintptr)st);
}

static void
i2cinit(void)
{
	i2c.regs = (Bsc*)I2CREGS;
	i2c.regs->clkdiv = 2500;

	gpiosel(SDA0Pin, Alt0);
	gpiosel(SCL0Pin, Alt0);
	gpiopullup(SDA0Pin);
	gpiopullup(SCL0Pin);

	intrenable(IRQi2c, i2cinterrupt, 0, 0, "i2c");
}

/*
 * To do subaddressing avoiding a STOP condition between the address and payload.
 * 	- write the subaddress,
 *	- poll until the transfer starts,
 *	- overwrite the registers for the payload transfer, before the subaddress
 * 		transaction has completed.
 *
 * FIXME: neither 10bit adressing nor 100Khz transfers implemented yet.
 */
static void
i2cio(int rw, int tenbit, uint addr, void *buf, int len, int salen, uint subaddr)
{
	Bsc *r;
	uchar *p;
	int st;

	if(tenbit)
		error("10bit addressing not supported");
	if(salen == 0 && subaddr)	/* default subaddress len == 1byte */
		salen = 1;

	qlock(&i2c.lock);
	r = i2c.regs;
	r->ctrl = I2cen | Clear;
	r->addr = addr;
	r->stat = Clkt|Err|Done;

	if(salen){
		r->dlen = salen;
		r->ctrl = I2cen | Start | Write;
		while((r->stat & Ta) == 0) {
			if(r->stat & (Err|Clkt)) {
				qunlock(&i2c.lock);
				error(Eio);
			}
		}
		r->dlen = len;
		r->ctrl = I2cen | Start | Intd | rw;
		for(; salen > 0; salen--)
			r->fifo = subaddr >> ((salen-1)*8);
		/*
		 * Adapted from Linux code...uses undocumented
		 * status information.
		 */
		if(rw == Read) {
			do {
				if(r->stat & (Err|Clkt)) {
					qunlock(&i2c.lock);
					error(Eio);
				}
				st = r->stat >> 28;
			} while(st != 0 && st != 4 && st != 5);
		}
	}
	else {
		r->dlen = len;
		r->ctrl = I2cen | Start | Intd | rw;
	}

	p = buf;
	st = rw == Read? Rxd : Txd;
	while(len > 0){
		while((r->stat & (st|Done)) == 0){
			r->ctrl |= rw == Read? Intr : Intt;
			sleep(&i2c.r, i2cready, (void*)(st|Done));
		}
		if(r->stat & (Err|Clkt)){
			qunlock(&i2c.lock);
			error(Eio);
		}
		if(rw == Read){
			do{
				*p++ = r->fifo;
				len--;
			}while ((r->stat & Rxd) && len > 0);
		}else{
			do{
				r->fifo = *p++;
				len--;
			}while((r->stat & Txd) && len > 0);
		}
	}
	while((r->stat & Done) == 0)
		sleep(&i2c.r, i2cready, (void*)Done);
	if(r->stat & (Err|Clkt)){
		qunlock(&i2c.lock);
		error(Eio);
	}
	r->ctrl = 0;
	qunlock(&i2c.lock);
}


void
i2csetup(int)
{
	//print("i2csetup\n");
	i2cinit();
}

long
i2csend(I2Cdev *d, void *buf, long len, ulong offset)
{
	i2cio(Write, d->tenbit, d->addr, buf, len, d->salen, offset);
	return len;
}

long
i2crecv(I2Cdev *d, void *buf, long len, ulong offset)
{
	i2cio(Read, d->tenbit, d->addr, buf, len, d->salen, offset);
	return len;
}
3:	/* Rd := FPSR */
			REG(ur, rd) = ufp->status;
			if(fpemudebug)
				print("MOVW	FPSR, R%d\n", rd);
			break;
		case 4:	/* FPCR := Rd */
			ufp->control = REG(ur, rd);
			if(fpemudebug)
				print("MOVW	R%d, FPCR\n", rd);
			break;
		case 5:	/* Rd := FPCR */
			REG(ur, rd) = ufp->control;
			if(fpemudebug)
				print("MOVW	FPCR, R%d\n", rd);
	init9.s                                                                                                664       0       0           33 12040231002  10346                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../omap/init9.s"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     io.h                                                                                                   664       0       0         2066 13527767637  10025                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    enum {
	IRQtimer0	= 0,
	IRQtimer1	= 1,
	IRQtimer2	= 2,
	IRQtimer3	= 3,
	IRQclock	= IRQtimer3,
	IRQusb		= 9,
	IRQdma0		= 16,
#define IRQDMA(chan)	(IRQdma0+(chan))
	IRQaux		= 29,
	IRQi2c		= 53,
	IRQspi		= 54,
	IRQsdhost	= 56,
	IRQmmc		= 62,

	IRQbasic	= 64,
	IRQtimerArm	= IRQbasic + 0,

	IRQether	= 93,

	IRQlocal	= 96,
	IRQcntps	= IRQlocal + 0,
	IRQcntpns	= IRQlocal + 1,
	IRQmbox0	= IRQlocal + 4,
	IRQmbox1	= IRQlocal + 5,
	IRQmbox2	= IRQlocal + 6,
	IRQmbox3	= IRQlocal + 7,
	IRQlocaltmr	= IRQlocal + 11,

	IRQfiq		= IRQusb,	/* only one source can be FIQ */

	DmaD2M		= 0,		/* device to memory */
	DmaM2D		= 1,		/* memory to device */
	DmaM2M		= 2,		/* memory to memory */

	DmaChanEmmc	= 4,		/* can only use 2-5, maybe 0 */
	DmaChanSdhost	= 5,
	DmaChanSpiTx= 2,
	DmaChanSpiRx= 0,

	DmaDevSpiTx	= 6,
	DmaDevSpiRx	= 7,
	DmaDevEmmc	= 11,
	DmaDevSdhost	= 13,

	PowerSd		= 0,
	PowerUart0,
	PowerUart1,
	PowerUsb,
	PowerI2c0,
	PowerI2c1,
	PowerI2c2,
	PowerSpi,
	PowerCcp2tx,

	ClkEmmc		= 1,
	ClkUart,
	ClkArm,
	ClkCore,
	ClkV3d,
	ClkH264,
	ClkIsp,
	ClkSdram,
	ClkPixel,
	ClkPwm,
};
                                                                                                                                                                                                                                                                                                                                                                                                                                                                          kbd.c                                                                                                  664       0       0           31 12020724115  10042                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../omap/kbd.c"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       l.s                                                                                                    664       0       0         1127 12666007606   7643                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Common startup for armv6 and armv7
 * The rest of l.s has been moved to armv[67].s
 */

#include "arm.s"

/*
 * on bcm2836, only cpu0 starts here
 * other cpus enter at cpureset in armv7.s
 */
TEXT _start(SB), 1, $-4
	/*
	 * load physical base for SB addressing while mmu is off
	 * keep a handy zero in R0 until first function call
	 */
	MOVW	$setR12(SB), R12
	SUB	$KZERO, R12
	ADD	$PHYSDRAM, R12
	MOVW	$0, R0

	/*
	 * start stack at top of mach (physical addr)
	 */
	MOVW	$PADDR(MACHADDR+MACHSIZE-4), R13

	/*
	 * do arch-dependent startup (no return)
	 */
	BL	,armstart(SB)
	B	,0(PC)

	RET
.
 *	i2c0 SDA0/SCL0 pins are not routed to P1 connector (except for early Rev 0 boards)
 *
 * maybe hardware problems lurking, see: https://github.com/raspberrypi/linux/issues/254
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#define I2CREGS	(VIRTIO+0x804000)
#define SDA0Pin	2
#define	SCL0Pin	3
#define	Alt0	0x4

typedef struct I2lexception.s                                                                                           664       0       0        16236 12501050617  11575                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * arm exception handlers
 */
#include "arm.s"

/*
 *  exception vectors, copied by trapinit() to somewhere useful
 */
TEXT vectors(SB), 1, $-4
	MOVW	0x18(R15), R15		/* reset */
	MOVW	0x18(R15), R15		/* undefined instr. */
	MOVW	0x18(R15), R15		/* SWI & SMC */
	MOVW	0x18(R15), R15		/* prefetch abort */
	MOVW	0x18(R15), R15		/* data abort */
	MOVW	0x18(R15), R15		/* reserved */
	MOVW	0x18(R15), R15		/* IRQ */
	MOVW	0x18(R15), R15		/* FIQ */

TEXT vtable(SB), 1, $-4
	WORD	$_vsvc(SB)		/* reset, in svc mode already */
	WORD	$_vund(SB)		/* undefined, switch to svc mode */
	WORD	$_vsvc(SB)		/* swi, in svc mode already */
	WORD	$_vpabt(SB)		/* prefetch abort, switch to svc mode */
	WORD	$_vdabt(SB)		/* data abort, switch to svc mode */
	WORD	$_vsvc(SB)		/* reserved */
	WORD	$_virq(SB)		/* IRQ, switch to svc mode */
	WORD	$_vfiq(SB)		/* FIQ, switch to svc mode */

TEXT _vsvc(SB), 1, $-4			/* SWI */
	CLREX
	MOVW.W	R14, -4(R13)		/* ureg->pc = interrupted PC */
	MOVW	SPSR, R14		/* ureg->psr = SPSR */
	MOVW.W	R14, -4(R13)		/* ... */
	MOVW	$PsrMsvc, R14		/* ureg->type = PsrMsvc */
	MOVW.W	R14, -4(R13)		/* ... */

	/* avoid the ambiguity described in notes/movm.w. */
	MOVM.DB.S [R0-R14], (R13)	/* save user level registers */
	SUB	$(15*4), R13		/* r13 now points to ureg */

	MOVW	$setR12(SB), R12	/* Make sure we've got the kernel's SB loaded */

	/* get R(MACH) for this cpu */
	CPUID(R1)
	SLL	$2, R1			/* convert to word index */
	MOVW	$machaddr(SB), R2
	ADD	R1, R2
	MOVW	(R2), R(MACH)		/* m = machaddr[cpuid] */
	CMP	$0, R(MACH)
	MOVW.EQ	$MACHADDR, R0		/* paranoia: use MACHADDR if 0 */

	MOVW	8(R(MACH)), R(USER)		/* up */

	MOVW	R13, R0			/* first arg is pointer to ureg */
	SUB	$8, R13			/* space for argument+link */

	BL	syscall(SB)

	ADD	$(8+4*15), R13		/* make r13 point to ureg->type */
	MOVW	8(R13), R14		/* restore link */
	MOVW	4(R13), R0		/* restore SPSR */
	MOVW	R0, SPSR		/* ... */
	MOVM.DB.S (R13), [R0-R14]	/* restore registers */
	ADD	$8, R13			/* pop past ureg->{type+psr} */
	RFE				/* MOVM.IA.S.W (R13), [R15] */

TEXT _vund(SB), 1, $-4			/* undefined */
	MOVM.IA	[R0-R4], (R13)		/* free some working space */
	MOVW	$PsrMund, R0
	B	_vswitch

TEXT _vpabt(SB), 1, $-4			/* prefetch abort */
	MOVM.IA	[R0-R4], (R13)		/* free some working space */
	MOVW	$PsrMabt, R0		/* r0 = type */
	B	_vswitch

TEXT _vdabt(SB), 1, $-4			/* data abort */
	MOVM.IA	[R0-R4], (R13)		/* free some working space */
	MOVW	$(PsrMabt+1), R0	/* r0 = type */
	B	_vswitch

TEXT _virq(SB), 1, $-4			/* IRQ */
	MOVM.IA	[R0-R4], (R13)		/* free some working space */
	MOVW	$PsrMirq, R0		/* r0 = type */
	B	_vswitch

	/*
	 *  come here with type in R0 and R13 pointing above saved [r0-r4].
	 *  we'll switch to SVC mode and then call trap.
	 */
_vswitch:
	CLREX
	MOVW	SPSR, R1		/* save SPSR for ureg */
	MOVW	R14, R2			/* save interrupted pc for ureg */
	MOVW	R13, R3			/* save pointer to where the original [R0-R4] are */

	/*
	 * switch processor to svc mode.  this switches the banked registers
	 * (r13 [sp] and r14 [link]) to those of svc mode.
	 */
	MOVW	CPSR, R14
	BIC	$PsrMask, R14
	ORR	$(PsrDirq|PsrMsvc), R14
	MOVW	R14, CPSR		/* switch! */

	AND.S	$0xf, R1, R4		/* interrupted code kernel or user? */
	BEQ	_userexcep

	/* here for trap from SVC mode */
	MOVM.DB.W [R0-R2], (R13)	/* set ureg->{type, psr, pc}; r13 points to ureg->type  */
	MOVM.IA	  (R3), [R0-R4]		/* restore [R0-R4] from previous mode's stack */

	/*
	 * avoid the ambiguity described in notes/movm.w.
	 * In order to get a predictable value in R13 after the stores,
	 * separate the store-multiple from the stack-pointer adjustment.
	 * We'll assume that the old value of R13 should be stored on the stack.
	 */
	/* save kernel level registers, at end r13 points to ureg */
	MOVM.DB	[R0-R14], (R13)
	SUB	$(15*4), R13		/* SP now points to saved R0 */

	MOVW	$setR12(SB), R12	/* Make sure we've got the kernel's SB loaded */

	MOVW	R13, R0			/* first arg is pointer to ureg */
	SUB	$(4*2), R13		/* space for argument+link (for debugger) */
	MOVW	$0xdeaddead, R11	/* marker */

	BL	trap(SB)

	MOVW	$setR12(SB), R12	/* reload kernel's SB (ORLY?) */
	ADD	$(4*2+4*15), R13	/* make r13 point to ureg->type */
	/*
	 * if we interrupted a previous trap's handler and are now
	 * returning to it, we need to propagate the current R(MACH) (R10)
	 * by overriding the saved one on the stack, since we may have
	 * been rescheduled and be on a different processor now than
	 * at entry.
	 */
	MOVW	R(MACH), (-(15-MACH)*4)(R13) /* restore current cpu's MACH */
	MOVW	8(R13), R14		/* restore link */
	MOVW	4(R13), R0		/* restore SPSR */
	MOVW	R0, SPSR		/* ... */

	MOVM.DB (R13), [R0-R14]		/* restore registers */

	ADD	$(4*2), R13		/* pop past ureg->{type+psr} to pc */
	RFE				/* MOVM.IA.S.W (R13), [R15] */

	/* here for trap from USER mode */
_userexcep:
	MOVM.DB.W [R0-R2], (R13)	/* set ureg->{type, psr, pc}; r13 points to ureg->type  */
	MOVM.IA	  (R3), [R0-R4]		/* restore [R0-R4] from previous mode's stack */

	/* avoid the ambiguity described in notes/movm.w. */
	MOVM.DB.S [R0-R14], (R13)	/* save kernel level registers */
	SUB	$(15*4), R13		/* r13 now points to ureg */

	MOVW	$setR12(SB), R12	/* Make sure we've got the kernel's SB loaded */

	/* get R(MACH) for this cpu */
	CPUID(R1)
	SLL	$2, R1			/* convert to word index */
	MOVW	$machaddr(SB), R2
	ADD	R1, R2
	MOVW	(R2), R(MACH)		/* m = machaddr[cpuid] */
	CMP	$0, R(MACH)
	MOVW.EQ	$MACHADDR, R(MACH)		/* paranoia: use MACHADDR if 0 */

	MOVW	8(R(MACH)), R(USER)		/* up */

	MOVW	R13, R0			/* first arg is pointer to ureg */
	SUB	$(4*2), R13		/* space for argument+link (for debugger) */

	BL	trap(SB)

	ADD	$(4*2+4*15), R13	/* make r13 point to ureg->type */
	MOVW	8(R13), R14		/* restore link */
	MOVW	4(R13), R0		/* restore SPSR */
	MOVW	R0, SPSR		/* ... */
	MOVM.DB.S (R13), [R0-R14]	/* restore registers */
	ADD	$(4*2), R13		/* pop past ureg->{type+psr} */
	RFE				/* MOVM.IA.S.W (R13), [R15] */

TEXT _vfiq(SB), 1, $-4			/* FIQ */
	CLREX
	MOVW	$PsrMfiq, R8		/* trap type */
	MOVW	SPSR, R9		/* interrupted psr */
	MOVW	R14, R10		/* interrupted pc */
	MOVM.DB.W [R8-R10], (R13)	/* save in ureg */
	MOVM.DB.S [R0-R14], (R13)	/* save interrupted regs */
	SUB	$(15*4), R13
	MOVW	$setR12(SB), R12	/* Make sure we've got the kernel's SB loaded */
	/* get R(MACH) for this cpu */
	CPUID(R1)
	SLL	$2, R1			/* convert to word index */
	MOVW	$machaddr(SB), R2
	ADD	R1, R2
	MOVW	(R2), R(MACH)		/* m = machaddr[cpuid] */
	CMP	$0, R(MACH)
	MOVW.EQ	$MACHADDR, R(MACH)		/* paranoia: use MACHADDR if 0 */

	MOVW	8(R(MACH)), R(USER)		/* up */
	MOVW	R13, R0			/* first arg is pointer to ureg */
	SUB	$(4*2), R13		/* space for argument+link (for debugger) */

	BL	fiq(SB)

	ADD	$(8+4*15), R13		/* make r13 point to ureg->type */
	MOVW	8(R13), R14		/* restore link */
	MOVW	4(R13), R0		/* restore SPSR */
	MOVW	R0, SPSR		/* ... */
	MOVM.DB.S (R13), [R0-R14]	/* restore registers */
	ADD	$8, R13			/* pop past ureg->{type+psr} */
	RFE				/* MOVM.IA.S.W (R13), [R15] */

/*
 *  set the stack value for the mode passed in R0
 */
TEXT setr13(SB), 1, $-4
	MOVW	4(FP), R1

	MOVW	CPSR, R2
	BIC	$PsrMask, R2, R3
	ORR	$(PsrDirq|PsrDfiq), R3
	ORR	R0, R3
	MOVW	R3, CPSR		/* switch to new mode */

	MOVW	R13, R0			/* return old sp */
	MOVW	R1, R13			/* install new one */

	MOVW	R2, CPSR		/* switch back to old mode */
	RET
                                                                                                   ustar 00miller                          sys                                                                                                                                                                                                                    lproc.s                                                                                                664       0       0           33 12040041726  10445                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../omap/lproc.s"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     main.c                                                                                                 664       0       0        34723 13527770056  10347                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "io.h"
#include "fns.h"

#include "init.h"
#include <pool.h>

#include "reboot.h"

enum {
	/* space for syscall args, return PC, top-of-stack struct */
	Ustkheadroom	= sizeof(Sargs) + sizeof(uintptr) + sizeof(Tos),
};

/* Firmware compatibility */
#define	Minfirmrev	326770
#define	Minfirmdate	"19 Aug 2013"

/*
 * Where configuration info is left for the loaded programme.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(MACHADDR-CONFADDR)
#define	MAXCONF		64
#define MAXCONFLINE	160

uintptr kseg0 = KZERO;
Mach*	machaddr[MAXMACH];
Conf	conf;
ulong	memsize = 128*1024*1024;

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 */
static int oargc;
static char* oargv[20];
static char oargb[128];
static int oargblen;

static uintptr sp;		/* XXX - must go - user stack of init proc */

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

typedef struct Atag Atag;
struct Atag {
	u32int	size;	/* size of atag in words, including this header */
	u32int	tag;	/* atag type */
	union {
		u32int	data[1];	/* actually [size-2] */
		/* AtagMem */
		struct {
			u32int	size;
			u32int	base;
		} mem;
		/* AtagCmdLine */
		char	cmdline[1];	/* actually [4*(size-2)] */
	};
};

enum {
	AtagNone	= 0x00000000,
	AtagCore	= 0x54410001,
	AtagMem		= 0x54410002,
	AtagCmdline	= 0x54410009,
};

static int
findconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return i;
	return -1;
}

char*
getconf(char *name)
{
	int i;

	i = findconf(name);
	if(i >= 0)
		return confval[i];
	return nil;
}

void
addconf(char *name, char *val)
{
	int i;

	i = findconf(name);
	if(i < 0){
		if(val == nil || nconf >= MAXCONF)
			return;
		i = nconf++;
		strecpy(confname[i], confname[i]+sizeof(confname[i]), name);
	}
	strecpy(confval[i], confval[i]+sizeof(confval[i]), val);
}

static void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();

	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memmove(BOOTARGS, p, n);
	memset(BOOTARGS + n, '\n', BOOTARGSLEN - n);
	poperror();
	free(p);
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

static void
ataginit(Atag *a)
{
	int n;

	if(a->tag != AtagCore){
		plan9iniinit((char*)a, 0);
		return;
	}
	while(a->tag != AtagNone){
		switch(a->tag){
		case AtagMem:
			/* use only first bank */
			if(conf.mem[0].limit == 0 && a->mem.size != 0){
				memsize = a->mem.size;
				conf.mem[0].base = a->mem.base;
				conf.mem[0].limit = a->mem.base + memsize;
			}
			break;
		case AtagCmdline:
			n = (a->size * sizeof(u32int)) - offsetof(Atag, cmdline[0]);
			if(a->cmdline + n < BOOTARGS + BOOTARGSLEN)
				a->cmdline[n] = 0;
			else
				BOOTARGS[BOOTARGSLEN-1] = 0;
			plan9iniinit(a->cmdline, 1);
			break;
		}
		a = (Atag*)((u32int*)a + a->size);
	}
}

/* enable scheduling of this cpu */
void
machon(uint cpu)
{
	ulong cpubit;

	cpubit = 1 << cpu;
	lock(&active);
	if ((active.machs & cpubit) == 0) {	/* currently off? */
		conf.nmach++;
		active.machs |= cpubit;
	}
	unlock(&active);
}

/* disable scheduling of this cpu */
void
machoff(uint cpu)
{
	ulong cpubit;

	cpubit = 1 << cpu;
	lock(&active);
	if (active.machs & cpubit) {		/* currently on? */
		conf.nmach--;
		active.machs &= ~cpubit;
	}
	unlock(&active);
}

void
machinit(void)
{
	Mach *m0;

	m->ticks = 1;
	m->perf.period = 1;
	m0 = MACHP(0);
	if (m->machno != 0) {
		/* synchronise with cpu 0 */
		m->ticks = m0->ticks;
	}
}

void
mach0init(void)
{
	conf.nmach = 0;

	m->machno = 0;
	machaddr[m->machno] = m;

	machinit();
	active.exiting = 0;

	up = nil;
}

void
launchinit(int ncpus)
{
	int mach;
	Mach *mm;
	PTE *l1;

	if(ncpus > MAXMACH)
		ncpus = MAXMACH;
	for(mach = 1; mach < ncpus; mach++){
		machaddr[mach] = mm = mallocalign(MACHSIZE, MACHSIZE, 0, 0);
		l1 = mallocalign(L1SIZE, L1SIZE, 0, 0);
		if(mm == nil || l1 == nil)
			panic("launchinit");
		memset(mm, 0, MACHSIZE);
		mm->machno = mach;

		memmove(l1, m->mmul1, L1SIZE);  /* clone cpu0's l1 table */
		cachedwbse(l1, L1SIZE);
		mm->mmul1 = l1;
		cachedwbse(mm, MACHSIZE);

	}
	cachedwbse(machaddr, sizeof machaddr);
	if((mach = startcpus(ncpus)) < ncpus)
			print("only %d cpu%s started\n", mach, mach == 1? "" : "s");
}

static void
optionsinit(char* s)
{
	strecpy(oargb, oargb+sizeof(oargb), s);

	oargblen = strlen(oargb);
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

void
main(void)
{
	extern char edata[], end[];
	uint fw, board;

	m = (Mach*)MACHADDR;
	memset(edata, 0, end - edata);	/* clear bss */
	mach0init();
	m->mmul1 = (PTE*)L1;
	machon(0);

	optionsinit("/boot/boot boot");
	quotefmtinstall();
	
	ataginit((Atag*)BOOTARGS);
	confinit();		/* figures out amount of memory */
	xinit();
	uartconsinit();
	screeninit();

	print("\nPlan 9 from Bell Labs\n");
	board = getboardrev();
	fw = getfirmware();
	print("board rev: %#ux firmware rev: %d\n", board, fw);
	if(fw < Minfirmrev){
		print("Sorry, firmware (start*.elf) must be at least rev %d"
		      " or newer than %s\n", Minfirmrev, Minfirmdate);
		for(;;)
			;
	}
	/* set clock rate to arm_freq from config.txt (default pi1:700Mhz pi2:900MHz) */
	setclkrate(ClkArm, 0);
	trapinit();
	clockinit();
	printinit();
	timersinit();
	if(conf.monitor)
		swcursorinit();
	cpuidprint();
	print("clocks: CPU %lud core %lud UART %lud EMMC %lud\n",
		getclkrate(ClkArm), getclkrate(ClkCore), getclkrate(ClkUart), getclkrate(ClkEmmc));
	archreset();
	vgpinit();

	procinit0();
	initseg();
	links();
	chandevreset();			/* most devices are discovered here */
	pageinit();
	swapinit();
	userinit();
	launchinit(getncpus());
	mmuinit1();

	schedinit();
	assert(0);			/* shouldn't have returned */
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	int i;
	Chan *c;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;
	coherence();
	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		snprint(buf, sizeof(buf), "-a %s", getethermac());
		ksetenv("etherargs", buf, 0);

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		if(getconf("pitft")){
			c = namec("#P/pitft", Aopen, OWRITE, 0);
			if(!waserror()){
				devtab[c->type]->write(c, "init", 4, 0);
				poperror();
			}
			cclose(c);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
	assert(0);			/* shouldn't have returned */
}

static void
bootargs(uintptr base)
{
	int i;
	ulong ssize;
	char **av, *p;

	/*
	 * Push the boot args onto the stack.
	 * The initial value of the user stack must be such
	 * that the total used is larger than the maximum size
	 * of the argument list checked in syscall.
	 */
	i = oargblen+1;
	p = UINT2PTR(STACKALIGN(base + BY2PG - Ustkheadroom - i));
	memmove(p, oargb, i);

	/*
	 * Now push the argv pointers.
	 * The code jumped to by touser in lproc.s expects arguments
	 *	main(char* argv0, ...)
	 * and calls
	 * 	startboot("/boot/boot", &argv0)
	 * not the usual (int argc, char* argv[])
	 */
	av = (char**)(p - (oargc+1)*sizeof(char*));
	ssize = base + BY2PG - PTR2UINT(av);
	for(i = 0; i < oargc; i++)
		*av++ = (oargv[i] - oargb) + (p - base) + (USTKTOP - BY2PG);
	*av = nil;
	sp = USTKTOP - ssize;
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->s.args)-sizeof(uintptr));
	p->sched.sp = STACKALIGN(p->sched.sp);

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	s->flushme++;
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
confinit(void)
{
	int i;
	ulong kpages;
	uintptr pa;
	char *p;

	if(0 && (p = getconf("service")) != nil){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}
	if((p = getconf("*maxmem")) != nil){
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}
	/*
	 * pi4 extra memory (beyond video ram) indicated by board id
	 */
	switch(getboardrev()&0xFFFFFF){
	case 0xA03111:
		break;
	case 0xB03111:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit = 2*GiB;
		break;
	case 0xC03111:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit = 0xFFFF0000;
		break;
	}
	if(conf.mem[1].limit > soc.dramsize)
		conf.mem[1].limit = soc.dramsize;
	if(p != nil){
		if(memsize < conf.mem[0].limit){
			conf.mem[0].limit = memsize;
			conf.mem[1].limit = 0;
		}else if(memsize >= conf.mem[1].base && memsize < conf.mem[1].limit)
			conf.mem[1].limit = memsize;
	}

	conf.npage = 0;
	pa = PADDR(PGROUND(PTR2UINT(end)));

	/*
	 *  we assume that the kernel is at the beginning of one of the
	 *  contiguous chunks of memory and fits therein.
	 */
	for(i=0; i<nelem(conf.mem); i++){
		/* take kernel out of allocatable space */
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/BY2PG;
		conf.npage += conf.mem[i].npage;
	}

	conf.upages = (conf.npage*80)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 1;		/* copy on reference, not copy on write */

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page);
	mainmem->maxsize = kpages;
	if(!cpuserver)
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;

}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once) {
		delay(m->machno*100);		/* stagger them */
		iprint("cpu%d: exiting\n", m->machno);
	}
	spllo();
	if (m->machno == 0)
		ms = 5*1000;
	else
		ms = 2*1000;
	for(; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}
	if(active.ispanic){
		if(!cpuserver)
			for(;;)
				;
		if(getconf("*debug"))
			delay(5*60*1000);
		else
			delay(10000);
	}
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int code)
{
	void (*f)(ulong, ulong, ulong);

	shutdown(code);
	splfhi();
	if(m->machno == 0)
		archreboot();
	else{
		f = (void*)REBOOTADDR;
		intrcpushutdown();
		cacheuwbinv();
		(*f)(0, 0, 0);
		for(;;){}
	}
}

/*
 * stub for ../omap/devether.c
 */
int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p;
	int i;

	if(strcmp(class, "ether") != 0)
		return 0;
	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return (ctlrno == 0);
	isa->type = "";
	isa->nopt = tokenize(p, isa->opt, NISAOPT);
	for(i = 0; i < isa->nopt; i++){
		p = isa->opt[i];
		if(cistrncmp(p, "type=", 5) == 0)
			isa->type = p + 5;
	}
	return 1;
}

/*
 * the new kernel is already loaded at address `code'
 * of size `size' and entry point `entry'.
 */
void
reboot(void *entry, void *code, ulong size)
{
	void (*f)(ulong, ulong, ulong);

	writeconf();

	/*
	 * the boot processor is cpu0.  execute this function on it
	 * so that the new kernel has the same cpu0.
	 */
	if (m->machno != 0) {
		procwired(up, 0);
		sched();
	}
	if (m->machno != 0)
		print("on cpu%d (not 0)!\n", m->machno);

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cachedwbse(f, sizeof(rebootcode));

	shutdown(0);

	/*
	 * should be the only processor running now
	 */

	delay(500);
	print("reboot entry %#lux code %#lux size %ld\n",
		PADDR(entry), PADDR(code), size);
	delay(100);

	/* turn off buffered serial console */
	serialoq = nil;
	kprintoq = nil;
	screenputs = nil;

	/* shutdown devices */
	if(!waserror()){
		chandevshutdown();
		poperror();
	}

	/* stop the clock (and watchdog if any) */
	clockshutdown();

	splfhi();
	intrshutdown();

	/* off we go - never to return */
	cacheuwbinv();
	l2cacheuwbinv();
	(*f)(PADDR(entry), PADDR(code), size);

	iprint("loaded kernel returned!\n");
	delay(1000);
	archreboot();
}
|| l1 == nil)
			panic("launchinit");
		memsemem.h                                                                                                  664       0       0         5355 13527777361  10173                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */

/*
 * Sizes
 */
#define	BY2PG		(4*KiB)			/* bytes per page */
#define	PGSHIFT		12			/* log(BY2PG) */

#define	MAXMACH		4			/* max # cpus system can run */
#define	MACHSIZE	BY2PG
#define L1SIZE		(4 * BY2PG)

#define KSTKSIZE	(8*KiB)
#define STACKALIGN(sp)	((sp) & ~3)		/* bug: assure with alloc */

/*
 * Magic registers
 */

#define	USER		9		/* R9 is up-> */
#define	MACH		10		/* R10 is m-> */

/*
 * Address spaces.
 * KTZERO is used by kprof and dumpstack (if any).
 *
 * KZERO is mapped to physical 0 (start of ram).
 *
 * vectors are at 0, plan9.ini is at KZERO+256 and is limited to 16K by
 * devenv.
 */

#define	KSEG0		0x80000000		/* kernel segment */
/* mask to check segment; good for 2GB dram */
#define	KSEGM		0x80000000
#define	KZERO		KSEG0			/* kernel address space */
#define CONFADDR	(KZERO+0x100)		/* unparsed plan9.ini */
#define	MACHADDR	(KZERO+0x2000)		/* Mach structure */
#define	L2		(KZERO+0x3000)		/* L2 ptes for vectors etc */
#define	VCBUFFER	(KZERO+0x3400)		/* videocore mailbox buffer */
#define	FIQSTKTOP	(KZERO+0x4000)		/* FIQ stack */
#define	L1		(KZERO+0x4000)		/* tt ptes: 16KiB aligned */
#define	KTZERO		(KZERO+0x8000)		/* kernel text start */
#define VIRTIO		0xFE000000		/* i/o registers */
#define	IOSIZE		(10*MiB)
#define	ARMLOCAL	(VIRTIO+IOSIZE)		/* armv7 only */
#define	VGPIO		(ARMLOCAL+MiB)		/* virtual gpio for pi3 ACT LED */
#define	FRAMEBUFFER	(VGPIO+MiB)		/* video framebuffer */

#define	UZERO		0			/* user segment */
#define	UTZERO		(UZERO+BY2PG)		/* user text start */
#define UTROUND(t)	ROUNDUP((t), BY2PG)
#define	USTKTOP		0x40000000		/* user segment end +1 */
#define	USTKSIZE	(8*1024*1024)		/* user stack size */
#define	TSTKTOP		(USTKTOP-USTKSIZE)	/* sysexec temporary stack */
#define	TSTKSIZ	 	256

/* address at which to copy and execute rebootcode */
#define	REBOOTADDR	(KZERO+0x1800)

/*
 * Legacy...
 */
#define BLOCKALIGN	64			/* only used in allocb.c */
#define KSTACK		KSTKSIZE

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BY2SE		4
#define BY2WD		4
#define BY2V		8			/* only used in xalloc.c */

#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	1984
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))

/*
 * With a little work these move to port.
 */
#define	PTEVALID	(1<<0)
#define	PTERONLY	0
#define	PTEWRITE	(1<<1)
#define	PTEUNCACHED	(1<<2)
#define PTEKERNEL	(1<<3)

/*
 * Physical machine information from here on.
 *	PHYS addresses as seen from the arm cpu.
 *	BUS  addresses as seen from the videocore gpu.
 */
#define	PHYSDRAM	0
                                                                                                                                                                                                                                                                                   mkfile                                                                                                 664       0       0         5150 13530461464  10413                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    CONF=pi2
CONFLIST=pi picpu pifat pi2 pi2cpu piwifi pi4 pi4cpu
EXTRACOPIES=

loadaddr=0x80008000

objtype=arm
</$objtype/mkfile
p=9

DEVS=`{rc ../port/mkdevlist $CONF|sort}

PORT=\
	alarm.$O\
	alloc.$O\
	allocb.$O\
	auth.$O\
	cache.$O\
	chan.$O\
	dev.$O\
	edf.$O\
	fault.$O\
	mul64fract.$O\
	page.$O\
	parse.$O\
	pgrp.$O\
	portclock.$O\
	print.$O\
	proc.$O\
	qio.$O\
	qlock.$O\
	random.$O\
	rdb.$O\
	rebootcmd.$O\
	segment.$O\
	swap.$O\
	syscallfmt.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\

OBJ=\
	l.$O\
	lexception.$O\
	lproc.$O\
	arch.$O\
	clock.$O\
	fpi.$O\
	fpiarm.$O\
	fpimem.$O\
	main.$O\
	mmu.$O\
	syscall.$O\
#	trap.$O\
	$DEVS\
	$PORT\
	$CONF.root.$O\
	$CONF.rootc.$O\

HFILES=\
	arm.h

LIB=\
	/$objtype/lib/libmemlayer.a\
	/$objtype/lib/libmemdraw.a\
	/$objtype/lib/libdraw.a\
	/$objtype/lib/libip.a\
	/$objtype/lib/libsec.a\
	/$objtype/lib/libmp.a\
	/$objtype/lib/libc.a\

9:V: $p$CONF s$p$CONF

$p$CONF:DQ:	$CONF.c $OBJ $LIB mkfile
	$CC $CFLAGS '-DKERNDATE='`{date -n} $CONF.c
	echo '# linking raw kernel'	# H6: no headers, data segment aligned
	$LD -s -l -o $target -H6 -R4096 -T$loadaddr $OBJ $CONF.$O $LIB

s$p$CONF:DQ:	$CONF.$O $OBJ $LIB
	echo '# linking kernel with symbols'
	$LD -l -o $target -R4096 -T$loadaddr $OBJ $CONF.$O $LIB
	size $target

$p$CONF.gz:D:	$p$CONF
	gzip -9 <$p$CONF >$target

$OBJ: $HFILES

install:V: /$objtype/$p$CONF

/$objtype/$p$CONF:D: $p$CONF s$p$CONF
	cp -x $p$CONF s$p$CONF /$objtype/ &
	for(i in $EXTRACOPIES)
		{ 9fs $i && cp $p$CONF s$p$CONF /n/$i/$objtype && echo -n $i... & }
	wait
	echo
	touch $target

<../boot/bootmkfile
<../port/portmkfile
<|../port/mkbootrules $CONF

arch.$O clock.$O fpiarm.$O main.$O mmu.$O screen.$O syscall.$O trap.$O trap4.$O: \
	/$objtype/include/ureg.h

archbcm.$O archbcm2.$O devether.$0 etherusb.$O: etherif.h ../port/netif.h
fpi.$O fpiarm.$O fpimem.$O: ../port/fpi.h
l.$O lexception.$O lproc.$O armv6.$O armv7.$O: arm.s
armv7.$O: cache.v7.s
main.$O: errstr.h init.h reboot.h
mouse.$O screen.$O: screen.h
devusb.$O usbdwc.$O: ../port/usb.h
usbdwc.$O: dwcotg.h

init.h:D:	../port/initcode.c init9.s
	$CC ../port/initcode.c
	$AS init9.s
	$LD -l -R1 -s -o init.out init9.$O initcode.$O /$objtype/lib/libc.a
	{echo 'uchar initcode[]={'
	 xd -1x <init.out |
		sed -e 's/^[0-9a-f]+ //' -e 's/ ([0-9a-f][0-9a-f])/0x\1,/g'
	 echo '};'} > init.h

reboot.h:D:	rebootcode.s arm.s arm.h mem.h
	$AS rebootcode.s
	# -lc is only for memmove.  -T arg is PADDR(REBOOTADDR)
	$LD -l -s -T0x1800 -R4 -o reboot.out rebootcode.$O -lc
	{echo 'uchar rebootcode[]={'
	 xd -1x reboot.out |
		sed -e '1,2d' -e 's/^[0-9a-f]+ //' -e 's/ ([0-9a-f][0-9a-f])/0x\1,/g'
	 echo '};'} > reboot.h

	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 1;		/* copy on reference, not copy on write */

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeofmmu.c                                                                                                  664       0       0        15605 13527744157  10222                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "arm.h"

#define L1X(va)		FEXT((va), 20, 12)
#define L2X(va)		FEXT((va), 12, 8)
#define L2AP(ap)	l2ap(ap)
#define L1ptedramattrs	soc.l1ptedramattrs
#define L2ptedramattrs	soc.l2ptedramattrs

enum {
	L1lo		= UZERO/MiB,		/* L1X(UZERO)? */
	L1hi		= (USTKTOP+MiB-1)/MiB,	/* L1X(USTKTOP+MiB-1)? */
	L2size		= 256*sizeof(PTE),
};

/*
 * Set up initial PTEs for cpu0 (called with mmu off)
 */
void
mmuinit(void *a)
{
	PTE *l1, *l2;
	uintptr pa, va;

	l1 = (PTE*)a;
	l2 = (PTE*)PADDR(L2);

	/*
	 * map all of ram at KZERO
	 */
	va = KZERO;
	for(pa = PHYSDRAM; pa < PHYSDRAM+soc.dramsize; pa += MiB){
		l1[L1X(va)] = pa|Dom0|L1AP(Krw)|Section|L1ptedramattrs;
		va += MiB;
	}

	/*
	 * identity map first MB of ram so mmu can be enabled
	 */
	l1[L1X(PHYSDRAM)] = PHYSDRAM|Dom0|L1AP(Krw)|Section|L1ptedramattrs;

	/*
	 * map i/o registers 
	 */
	va = VIRTIO;
	for(pa = soc.physio; pa < soc.physio+IOSIZE; pa += MiB){
		l1[L1X(va)] = pa|Dom0|L1AP(Krw)|Section|L1noexec;
		va += MiB;
	}
	pa = soc.armlocal;
	if(pa)
		l1[L1X(va)] = pa|Dom0|L1AP(Krw)|Section|L1noexec;
	/*
	 * pi4 hack: ether is in segment 0xFD50xxxx not 0xFE50xxxx
	 */
	va = VIRTIO + 0x500000;
	pa = soc.physio - 0x1000000 + 0x500000;
	l1[L1X(va)] = pa|Dom0|L1AP(Krw)|Section|L1noexec;
	
	/*
	 * double map exception vectors near top of virtual memory
	 */
	va = HVECTORS;
	l1[L1X(va)] = (uintptr)l2|Dom0|Coarse;
	l2[L2X(va)] = PHYSDRAM|L2AP(Krw)|Small|L2ptedramattrs;
}

void
mmuinit1()
{
	PTE *l1;

	l1 = m->mmul1;

	/*
	 * undo identity map of first MB of ram
	 */
	l1[L1X(PHYSDRAM)] = 0;
	cachedwbtlb(&l1[L1X(PHYSDRAM)], sizeof(PTE));
	mmuinvalidateaddr(PHYSDRAM);
}

static void
mmul2empty(Proc* proc, int clear)
{
	PTE *l1;
	Page **l2, *page;

	l1 = m->mmul1;
	l2 = &proc->mmul2;
	for(page = *l2; page != nil; page = page->next){
		if(clear)
			memset(UINT2PTR(page->va), 0, L2size);
		l1[page->daddr] = Fault;
		l2 = &page->next;
	}
	coherence();
	*l2 = proc->mmul2cache;
	proc->mmul2cache = proc->mmul2;
	proc->mmul2 = nil;
}

static void
mmul1empty(void)
{
	PTE *l1;

	/* clean out any user mappings still in l1 */
	if(m->mmul1lo > 0){
		if(m->mmul1lo == 1)
			m->mmul1[L1lo] = Fault;
		else
			memset(&m->mmul1[L1lo], 0, m->mmul1lo*sizeof(PTE));
		m->mmul1lo = 0;
	}
	if(m->mmul1hi > 0){
		l1 = &m->mmul1[L1hi - m->mmul1hi];
		if(m->mmul1hi == 1)
			*l1 = Fault;
		else
			memset(l1, 0, m->mmul1hi*sizeof(PTE));
		m->mmul1hi = 0;
	}
}

void
mmuswitch(Proc* proc)
{
	int x;
	PTE *l1;
	Page *page;

	if(proc != nil && proc->newtlb){
		mmul2empty(proc, 1);
		proc->newtlb = 0;
	}

	mmul1empty();

	/* move in new map */
	l1 = m->mmul1;
	if(proc != nil)
	for(page = proc->mmul2; page != nil; page = page->next){
		x = page->daddr;
		l1[x] = PPN(page->pa)|Dom0|Coarse;
		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	}

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&l1[L1X(UZERO)], (L1hi - L1lo)*sizeof(PTE));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

void
mmurelease(Proc* proc)
{
	Page *page, *next;

	mmul2empty(proc, 0);
	for(page = proc->mmul2cache; page != nil; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %d", page->ref);
		pagechainhead(page);
	}
	if(proc->mmul2cache && palloc.r.p)
		wakeup(&palloc.r);
	proc->mmul2cache = nil;

	mmul1empty();

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&m->mmul1[L1X(UZERO)], (L1hi - L1lo)*sizeof(PTE));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
putmmu(uintptr va, uintptr pa, Page* page)
{
	int x, s;
	Page *pg;
	PTE *l1, *pte;

	/*
	 * disable interrupts to prevent flushmmu (called from hzclock)
	 * from clearing page tables while we are setting them
	 */
	s = splhi();
	x = L1X(va);
	l1 = &m->mmul1[x];
	if(*l1 == Fault){
		/* l2 pages only have 256 entries - wastes 3K per 1M of address space */
		if(up->mmul2cache == nil){
			spllo();
			pg = newpage(1, 0, 0);
			splhi();
			/* if newpage slept, we might be on a different cpu */
			l1 = &m->mmul1[x];
			pg->va = VA(kmap(pg));
		}else{
			pg = up->mmul2cache;
			up->mmul2cache = pg->next;
		}
		pg->daddr = x;
		pg->next = up->mmul2;
		up->mmul2 = pg;

		/* force l2 page to memory (armv6) */
		cachedwbtlb((void *)pg->va, L2size);

		*l1 = PPN(pg->pa)|Dom0|Coarse;
		cachedwbtlb(l1, sizeof *l1);

		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	}
	pte = UINT2PTR(KADDR(PPN(*l1)));

	/* protection bits are
	 *	PTERONLY|PTEVALID;
	 *	PTEWRITE|PTEVALID;
	 *	PTEWRITE|PTEUNCACHED|PTEVALID;
	 */
	x = Small;
	if(!(pa & PTEUNCACHED))
		x |= L2ptedramattrs;
	if(pa & PTEWRITE)
		x |= L2AP(Urw);
	else
		x |= L2AP(Uro);
	pte[L2X(va)] = PPN(pa)|x;
	cachedwbtlb(&pte[L2X(va)], sizeof(PTE));

	/* clear out the current entry */
	mmuinvalidateaddr(PPN(va));

	if(page->cachectl[m->machno] == PG_TXTFLUSH){
		/* pio() sets PG_TXTFLUSH whenever a text pg has been written */
		cachedwbse((void*)(page->pa|KZERO), BY2PG);
		cacheiinvse((void*)page->va, BY2PG);
		page->cachectl[m->machno] = PG_NOFLUSH;
	}
	//checkmmu(va, PPN(pa));
	splx(s);
}

void*
mmuuncache(void* v, usize size)
{
	int x;
	PTE *pte;
	uintptr va;

	/*
	 * Simple helper for ucalloc().
	 * Uncache a Section, must already be
	 * valid in the MMU.
	 */
	va = PTR2UINT(v);
	assert(!(va & (1*MiB-1)) && size == 1*MiB);

	x = L1X(va);
	pte = &m->mmul1[x];
	if((*pte & (Fine|Section|Coarse)) != Section)
		return nil;
	*pte &= ~L1ptedramattrs;
	mmuinvalidateaddr(va);
	cachedwbinvse(pte, 4);

	return v;
}

/*
 * Return the number of bytes that can be accessed via KADDR(pa).
 * If pa is not a valid argument to KADDR, return 0.
 */
uintptr
cankaddr(uintptr pa)
{
	if(pa < PHYSDRAM + memsize)		/* assumes PHYSDRAM is 0 */
		return PHYSDRAM + memsize - pa;
	return 0;
}

uintptr
mmukmap(uintptr va, uintptr pa, usize size)
{
	int o;
	usize n;
	PTE *pte, *pte0;

	assert((va & (MiB-1)) == 0);
	o = pa & (MiB-1);
	pa -= o;
	size += o;
	pte = pte0 = &m->mmul1[L1X(va)];
	for(n = 0; n < size; n += MiB)
		if(*pte++ != Fault)
			return 0;
	pte = pte0;
	for(n = 0; n < size; n += MiB){
		*pte++ = (pa+n)|Dom0|L1AP(Krw)|Section;
		mmuinvalidateaddr(va+n);
	}
	cachedwbtlb(pte0, (uintptr)pte - (uintptr)pte0);
	return va + o;
}

void
checkmmu(uintptr va, uintptr pa)
{
	int x;
	PTE *l1, *pte;

	x = L1X(va);
	l1 = &m->mmul1[x];
	if(*l1 == Fault){
		iprint("checkmmu cpu%d va=%lux l1 %p=%ux\n", m->machno, va, l1, *l1);
		return;
	}
	pte = KADDR(PPN(*l1));
	pte += L2X(va);
	if(pa == ~0 || (pa != 0 && PPN(*pte) != pa))
		iprint("checkmmu va=%lux pa=%lux l1 %p=%ux pte %p=%ux\n", va, pa, l1, *l1, pte, *pte);
}

void
kunmap(KMap *k)
{
	USED(k);
	coherence();
}
	rdb.$O\
	rebootcmd.$O\
	segment.$O\
	swap.$O\
	syscallfmt.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\
mouse.c                                                                                                664       0       0           33 12020722112  10427                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../omap/mouse.c"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     pi                                                                                                     664       0       0         1264 13514062574   7557                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm
	loopbackmedium
	ethermedium
	sdhost
	usbdwc
	etherusb
	ether4330 emmc
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv6
	trap
	uartmini	gpio
	sdmmc	emmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
	/arm/bin/usb/usbd
nf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 1;		/* copy on reference, not copy on write */

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeofpi2                                                                                                    664       0       0         1265 13514062574   7642                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm2
	loopbackmedium
	ethermedium
	sdhost
	usbdwc
	etherusb
	ether4330 emmc
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap
	uartmini	gpio
	sdmmc	emmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
	/arm/bin/usb/usbd
		l1[L1X(va)] = pa|Dom0|L1AP(Krw)|Section|L1ptedramattrs;
		va += MiB;
	}

	/*
	 * identity map first MB of ram so mmu can be enabled
	 */
	l1[L1X(PHYSDRAM)] = PHYSDRAM|Dom0|L1AP(Krw)|Section|L1ptedramattrs;

	/*
	 * map i/o registers 
	 */
	va = VIRTIO;
	for(pa = soc.physio; pa < soc.physio+IOSIZE; pa += MiB){
		l1[L1X(va)] = papi2cpu                                                                                                 664       0       0         1271 13514062574  10347                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm2
	loopbackmedium
	ethermedium
	sdhost
	usbdwc
	etherusb
	ether4330 emmc
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap
	uartmini	gpio
	sdmmc	emmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 1;

boot cpu boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
	/arm/bin/usb/usbd
L1lo] = Fault;
		else
			memset(&m->mmul1[L1lo], 0, m->mmul1lo*sizeof(PTE));
		m->mmul1lo = 0;
	}
	if(m->mmul1hi > 0){
		l1 = &m->mmul1[L1hi - m->mmul1hi];
		if(m->mmul1hi == 1)
			*l1 = Fault;
		else
			memset(l1, 0, m->mmul1hi*sizeof(PTE));
		m->mmul1hi = 0;
	}
}

void
mmuswitch(Proc* proc)
{
	int x;
	PTE *l1;
	Page *page;
pi2wifi                                                                                                664       0       0         1534 13526734472  10526                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb

link
	archbcm2
	loopbackmedium
	ethermedium
	sdhost
	usbdwc
	etherusb
	ether4330 emmc
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap
	uartmini	gpio
	sdmmc	emmc
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	bootwifi.rc			boot
	/arm/bin/rc
	/rc/lib/rcmain
	/arm/bin/usb/usbd
	/arm/bin/auth/factotum
	/arm/bin/srv
	/arm/bin/aux/wpa wpa
	/arm/bin/ip/ipconfig
	/arm/bin/mount
	/arm/bin/bind
	/arm/bin/echo
	/arm/bin/read
	/sys/lib/firmware/brcmfmac43430-sdio.bin
	/sys/lib/firmware/brcmfmac43430-sdio.txt
s to prevent flushmmu (called from hzclock)
	 * from clearing page tables while we are setting them
	 */
	s = splhi();
	x = L1X(va);
	l1 = &m->mmul1[x];
	if(*l1 == pi4                                                                                                    664       0       0         1323 13530174205   7630                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
#	spi		spi
#	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm4
	loopbackmedium
	ethermedium
#	sdhost
	sdhc
#	usbdwc
#	etherusb
	ethergenet ethermii
	ether4330 emmc
#	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap4
	uartmini	gpio
	sdmmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
#	/arm/bin/usb/usbd
->pa|KZERO), BY2PG);
		cacheiinvse((void*)page->va, BY2PG);
		page->cachectl[m->machno] = PG_NOFLUSH;
	}
	//checkmmu(va, PPN(pa));
	splx(s);
}

void*
mmuuncache(void* v, usize size)
{
	int x;
	PTE *pte;
	uintptr va;

	/*
	 * Simple helper for ucalloc().
	 * Uncache a Section, must already be
	 * valipi4cpu                                                                                                 664       0       0         1327 13530227147  10350                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
#	spi		spi
#	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm4
	loopbackmedium
	ethermedium
#	sdhost
	sdhc
#	usbdwc
#	etherusb
	ethergenet ethermii
	ether4330 emmc
#	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap4
	uartmini	gpio
	sdmmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 1;

boot cpu boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
#	/arm/bin/usb/usbd
|| (pa != 0 && PPN(*pte) != pa))
		iprint("checkmmu va=%lux pa=%lux l1 %p=%ux pte %p=%ux\n", va, pa, l1, *l1, pte, *pte);
}

void
kunmap(KMap *k)
{
	USED(k);
	coherence();
}
	rdb.$O\
	rebootcmd.$O\
	segment.$O\
	swap.$O\
	syscallfmt.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\
pi4wifi                                                                                                664       0       0         1647 13521640431  10517                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
#	spi		spi
#	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm4
	loopbackmedium
	ethermedium
#	sdhost
#	usbdwc
#	etherusb
	ether4330 emmc
#	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv7
	trap4
	uartmini	gpio
#	sdmmc	emmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	bootwifi.rc			boot
	/arm/bin/rc
	/rc/lib/rcmain
#	/arm/bin/usb/usbd
	/arm/bin/auth/factotum
	/arm/bin/srv
	/arm/bin/aux/wpa wpa
	/arm/bin/ip/ipconfig
	/arm/bin/mount
	/arm/bin/bind
	/arm/bin/echo
	/arm/bin/read
	/sys/lib/firmware/brcmfmac43455-sdio.bin
	/sys/lib/firmware/brcmfmac43455-sdio.txt
	/sys/lib/firmware/brcmfmac43455-sdio.clm_blob
                                                                                         picpu                                                                                                  664       0       0         1270 13514062575  10265                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb
	aoe

link
	archbcm
	loopbackmedium
	ethermedium
	sdhost
	usbdwc
	etherusb
	ether4330 emmc
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv6
	trap
	uartmini	gpio
	sdmmc	emmc
	sdaoe	sdscsi
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 1;

boot cpu boot #S/sdM0/
	local
	tcp

bootdir
	boot$CONF.out	boot
	/arm/bin/ip/ipconfig
	/arm/bin/auth/factotum
	/arm/bin/fossil/fossil
	/arm/bin/usb/usbd
                                                                         ustar 00miller                          sys                                                                                                                                                                                                                    pifat                                                                                                  664       0       0         1106 13514062575  10246                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen vcore
	mouse	mouse
	kbin	kbd latin1
	uart

	fakertc
	sd
	usb

link
	archbcm
	loopbackmedium
	ethermedium
	usbdwc

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv6
	trap
	uartmini	gpio
	sdmmc	emmc
	dma
	vfp3	coproc

port
	int cpuserver = 0;

bootdir
	boot.rc			boot
	/arm/bin/rc
	/rc/lib/rcmain
	/arm/bin/mount
	/arm/bin/bind
	/arm/bin/echo
	/arm/bin/disk/fdisk
	/arm/bin/dossrv
	/arm/bin/ls
	/arm/bin/cat
	/arm/bin/usb/usbd


                                 664       0       0         1271 13514062574  10347                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    pitft.c                                                                                                664       0       0        11651 12677470230  10540                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Support for a SPI LCD panel from Adafruit
 * based on HX8357D controller chip
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	TFTWidth = 480,
	TFTHeight = 320,
};

static void pitftblank(int);
static void pitftdraw(Rectangle);
static long pitftread(Chan*, void*, long, vlong);
static long pitftwrite(Chan*, void*, long, vlong);
static void spicmd(uchar);
static void spidata(uchar *, int);
static void setwindow(int, int, int, int);
static void xpitftdraw(void *);

extern Memimage xgscreen;
extern Lcd *lcd;

static Lcd pitft = {
	pitftdraw,
	pitftblank,
};

static Queue *updateq = nil;

void
pitftlink(void)
{
	addarchfile("pitft", 0666, pitftread, pitftwrite);
}

static void
pitftsetup(void)
{
	uchar spibuf[32];

	gpiosel(25, Output);
	spirw(0, spibuf, 1);
	spicmd(0x01);
	delay(10);
	spicmd(0x11);
	delay(10);
	spicmd(0x29);
	spicmd(0x13);
	spicmd(0x36);
	spibuf[0] = 0xe8;
	spidata(spibuf, 1);
	spicmd(0x3a);
	spibuf[0] = 0x05;
	spidata(spibuf, 1);
}

static long
pitftread(Chan *, void *, long, vlong)
{
	return 0;
}

static long
pitftwrite(Chan *, void *a, long n, vlong)
{
	if(strncmp(a, "init", 4) == 0 && updateq == nil) {
		/*
		 * The HX8357 datasheet shows minimum
		 * clock cycle time of 66nS but the clock high
		 * and low times as 15nS and it seems to
		 * work at around 32MHz.
		 */
		spiclock(32);
		pitftsetup();
		updateq = qopen(16384, 1, nil, nil);
		kproc("pitft", xpitftdraw, nil);
		lcd = &pitft;
	}
	return n;
}

static void
pitftblank(int blank)
{
	USED(blank);
}

static void
pitftdraw(Rectangle r)
{
	if(updateq == nil)
		return;
	if(r.min.x > TFTWidth || r.min.y > TFTHeight)
		return;
	/*
	 * using qproduce to make sure we don't block
	 * but if we've got a lot on the queue, it means we're
	 * redrawing the same areas over and over; clear it
	 * out and just draw the whole screen once
	 */
	if(qproduce(updateq, &r, sizeof(Rectangle)) == -1) {
		r = Rect(0, 0, TFTWidth, TFTHeight);
		qflush(updateq);
		qproduce(updateq, &r, sizeof(Rectangle));
	}
}

int
overlap(Rectangle r1, Rectangle r2)
{
	if(r1.max.x < r2.min.x)
		return 0;
	if(r1.min.x > r2.max.x)
		return 0;
	if(r1.max.y < r2.min.y)
		return 0;
	if(r1.min.y > r2.max.y)
		return 0;
	return 1;
}

int
min(int x, int y)
{
	if(x < y)
		return x;
	return y;
}

int
max(int x, int y)
{
	if(x < y)
		return y;
	return x;
}

/*
 * Because everyone wants to be holding locks when
 * they update the screen but we need to sleep in the
 * SPI code, we're decoupling this into a separate kproc().
 */
static void
xpitftdraw(void *)
{
	Rectangle rec, bb;
	Point pt;
	uchar *p;
	int i, r, c, gotrec;
	uchar spibuf[32];

	gotrec = 0;
	qread(updateq, &rec, sizeof(Rectangle));
	bb = Rect(0, 0, TFTWidth, TFTHeight);
	while(1) {
		setwindow(bb.min.x, bb.min.y,
			bb.max.x-1, bb.max.y-1);
		spicmd(0x2c);
		for(r = bb.min.y; r < bb.max.y; ++r) {
			for(c = bb.min.x; c < bb.max.x; c += 8) {
				for(i = 0; i < 8; ++i) {
					pt.y = r;
					pt.x = c + i;
					p = byteaddr(&xgscreen, pt);
					switch(xgscreen.depth) {
					case 16:		// RGB16
						spibuf[i*2+1] = p[0];
						spibuf[i*2] = p[1];
						break;
					case 24:		// BGR24
						spibuf[i*2] = (p[2] & 0xf8) | 
							(p[1] >> 5);
						spibuf[i*2+1] = (p[0] >> 3) |
							(p[1] << 3);
						break;
					case 32:		// ARGB32
						spibuf[i*2] = (p[0] & 0xf8) | 
							(p[1] >> 5);
						spibuf[i*2+1] = (p[1] >> 3) |
							(p[1] << 3);
						break;
					}
				}
				spidata(spibuf, 16);
			}
		}
		bb.max.y = -1;
		while(1) {
			if(!gotrec) {
				qread(updateq, &rec, sizeof(Rectangle));
				gotrec = 1;
			}
			if(bb.max.y != -1) {
				if(!overlap(bb, rec))
					break;
				rec.min.x = min(rec.min.x, bb.min.x);
				rec.min.y = min(rec.min.y, bb.min.y);
				rec.max.x = max(rec.max.x, bb.max.x);
				rec.max.y = max(rec.max.y, bb.max.y);
			}
			gotrec = 0;
			// Expand rows to 8 pixel alignment
			bb.min.x = rec.min.x & ~7;
			if(bb.min.x < 0)
				bb.min.x = 0;
			bb.max.x = (rec.max.x + 7) & ~7;
			if(bb.max.x > TFTWidth)
				bb.max.x = TFTWidth;
			bb.min.y = rec.min.y;
			if(bb.min.y < 0)
				bb.min.y = 0;
			bb.max.y = rec.max.y;
			if(bb.max.y > TFTHeight)
				bb.max.y = TFTHeight;
			if(qcanread(updateq)) {
				qread(updateq, &rec, sizeof(Rectangle));
				gotrec = 1;
			}
			else
				break;
		}
	}
}

static void
spicmd(uchar c)
{
	char buf;

	gpioout(25, 0);
	buf = c;
	spirw(0, &buf, 1);
}

static void
spidata(uchar *p, int n)
{
	char buf[128];

	if(n > 128)
		n = 128;
	gpioout(25, 1);
	memmove(buf, p, n);
	spirw(0, buf, n);
	gpioout(25, 0);
}

static void
setwindow(int minc, int minr, int maxc, int maxr)
{
	uchar spibuf[4];

	spicmd(0x2a);
	spibuf[0] = minc >> 8;
	spibuf[1] = minc & 0xff;
	spibuf[2] = maxc >> 8;
	spibuf[3] = maxc & 0xff;
	spidata(spibuf, 4);
	spicmd(0x2b);
	spibuf[0] = minr >> 8;
	spibuf[1] = minr & 0xff;
	spibuf[2] = maxr >> 8;
	spibuf[3] = maxr & 0xff;
	spidata(spibuf, 4);
}

	swap.$O\
	syscallfmt.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\
piwifi                                                                                                 664       0       0         1533 13514062575  10436                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    dev
	root
	cons
	env
	pipe
	proc
	mnt
	srv
	dup
	arch
	ssl
	tls
	cap
	fs
	ip		arp chandial ip ipv6 ipaux iproute netlog nullmedium pktmedium ptclbsum inferno
	draw	screen
	mouse	mouse
	kbmap
	kbin	kbd latin1
	uart
	gpio	gpio
	spi		spi
	i2c		i2c

	fakertc
#	rtc3231		i2c
	ether	netif
	sd
	usb

link
	archbcm
	loopbackmedium
	ethermedium
	ether4330 emmc
	sdhost
	usbdwc
	etherusb
	pitft

ip
	tcp
	udp
	ipifc
	icmp
	icmp6
	ipmux

misc
	armv6
	trap
	uartmini	gpio
	sdmmc	emmc
	dma
	vcore
	vfp3	coproc

port
	int cpuserver = 0;

boot boot #S/sdM0/
	local
	tcp

bootdir
	bootwifi.rc			boot
	/arm/bin/rc
	/rc/lib/rcmain
	/arm/bin/usb/usbd
	/arm/bin/auth/factotum
	/arm/bin/srv
	/arm/bin/aux/wpa wpa
	/arm/bin/ip/ipconfig
	/arm/bin/mount
	/arm/bin/bind
	/arm/bin/echo
	/arm/bin/read
	/sys/lib/firmware/brcmfmac43430-sdio.bin
	/sys/lib/firmware/brcmfmac43430-sdio.txt
mware/brcmfmac43455-sdio.txt
	/sys/lib/firmware/brcmfmac43455-sdio.clm_blob
                                                                                         rebootcode.s                                                                                           664       0       0         7557 13305755224  11547                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * armv6/armv7 reboot code
 */
#include "arm.s"

#define PTEDRAM		(Dom0|L1AP(Krw)|Section)

#define WFI	WORD	$0xe320f003	/* wait for interrupt */
#define WFE	WORD	$0xe320f002	/* wait for event */

/*
 * CPU0:
 * Copy the new kernel to its correct location in virtual memory.
 * Then turn off the mmu and jump to the start of the kernel.
 *
 * Other CPUs:
 * Turn off the mmu, wait for a restart address from CPU0, and jump to it.
 */

/* main(PADDR(entry), PADDR(code), size); */
TEXT	main(SB), 1, $-4
	MOVW	$setR12(SB), R12

	/* copy in arguments before stack gets unmapped */
	MOVW	R0, R8			/* entry point */
	MOVW	p2+4(FP), R7		/* source */
	MOVW	n+8(FP), R6		/* byte count */

	/* redo double map of first MiB PHYSDRAM = KZERO */
	MOVW	12(R(MACH)), R2		/* m->mmul1 (virtual addr) */
	MOVW	$PTEDRAM, R1		/* PTE bits */
	MOVW	R1, (R2)
	DSB
	MCR	CpSC, 0, R2, C(CpCACHE), C(CpCACHEwb), CpCACHEse

	/* invalidate stale TLBs */
	BARRIERS
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpTLB), C(CpTLBinvu), CpTLBinv
	BARRIERS

	/* relocate PC to physical addressing */
	MOVW	$_reloc(SB), R15

TEXT _reloc(SB), $-4
	
	/* continue with reboot only on cpu0 */
	CPUID(R2)
	BEQ	bootcpu

	/* other cpus wait for inter processor interrupt from cpu0 */

	/* turn caches off, invalidate icache */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall
 	/* turn off mmu */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$CpCmmu, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	/* turn off SMP cache snooping */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BIC	$CpACsmp, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	ISB
	DSB
	/* turn icache back on */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	ORR	$(CpCicache), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS

dowfi:
	WFE			/* wait for event signal */
	MOVW	$0x400000CC, R1	/* inter-core .startcpu mailboxes */
	ADD	R2<<4, R1	/* mailbox for this core */
	MOVW	0(R1), R8	/* content of mailbox */
	CMP	$0, R8		
	BEQ	dowfi		/* if zero, wait again */
	BL	(R8)		/* call received address */
	B	dowfi		/* shouldn't return */

bootcpu:
	MOVW	$PADDR(MACHADDR+MACHSIZE-4), SP

	/* copy the kernel to final destination */
	MOVW	R8, R9		/* save physical entry point */
	ADD	$KZERO, R8	/* relocate dest to virtual */
	ADD	$KZERO, R7	/* relocate src to virtual */
	ADD	$3, R6		/* round length to words */
	BIC	$3, R6
memloop:
	MOVM.IA.W	(R7), [R1]
	MOVM.IA.W	[R1], (R8)
	SUB.S	$4, R6
	BNE	memloop

	/* clean dcache using appropriate code for armv6 or armv7 */
	MRC	CpSC, 0, R1, C(CpID), C(CpIDfeat), 7	/* Memory Model Feature Register 3 */
	TST	$0xF, R1	/* hierarchical cache maintenance? */
	BNE	l2wb
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEwb), CpCACHEall
	B	l2wbx
l2wb:
	BL		cachedwb(SB)
	BL		l2cacheuwb(SB)
l2wbx:

	/* turn caches off, invalidate icache */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$(CpCdcache|CpCicache|CpCpredict), R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvi), CpCACHEall
	DSB
 	/* turn off mmu */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BIC	$CpCmmu, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpMainctl
	BARRIERS
	/* turn off SMP cache snooping */
	MRC	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl
	BIC	$CpACsmp, R1
	MCR	CpSC, 0, R1, C(CpCONTROL), C(0), CpAuxctl

	/* invalidate dcache using appropriate code for armv6 or armv7 */
	MRC	CpSC, 0, R1, C(CpID), C(CpIDfeat), 7	/* Memory Model Feature Register 3 */
	TST	$0xF, R1	/* hierarchical cache maintenance */
	BNE	l2inv
	DSB
	MOVW	$0, R0
	MCR	CpSC, 0, R0, C(CpCACHE), C(CpCACHEinvd), CpCACHEall
	B	l2invx
l2inv:
	BL		cachedinv(SB)
	BL		l2cacheuinv(SB)
l2invx:

	/* jump to restart entry point */
	MOVW	R9, R8
	MOVW	$0, R9
	B	(R8)

#define ICACHELINESZ	32
#include "cache.v7.s"
;

	gpiosel(25, Output);
	spirw(0, spibuf, 1);
	spicmd(0x01);
	delay(10);
	spicmd(0x11);
	delay(10);
	spicmd(0x29);
	spicmd(0x13);
	spicmd(0x36);screen.c                                                                                               664       0       0        25601 12723014063  10657                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2385 framebuffer
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Tabstop		= 4,
	Scroll		= 8,
	Wid		= 1024,
	Ht		= 768,
	Depth		= 16,
};

Cursor	arrow = {
	{ -1, -1 },
	{ 0xFF, 0xFF, 0x80, 0x01, 0x80, 0x02, 0x80, 0x0C,
	  0x80, 0x10, 0x80, 0x10, 0x80, 0x08, 0x80, 0x04,
	  0x80, 0x02, 0x80, 0x01, 0x80, 0x02, 0x8C, 0x04,
	  0x92, 0x08, 0x91, 0x10, 0xA0, 0xA0, 0xC0, 0x40,
	},
	{ 0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFC, 0x7F, 0xF0,
	  0x7F, 0xE0, 0x7F, 0xE0, 0x7F, 0xF0, 0x7F, 0xF8,
	  0x7F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFC, 0x73, 0xF8,
	  0x61, 0xF0, 0x60, 0xE0, 0x40, 0x40, 0x00, 0x00,
	},
};

Memimage *gscreen;
Lcd	*lcd;

static Memdata xgdata;

/*static*/ Memimage xgscreen =
{
	{ 0, 0, Wid, Ht },	/* r */
	{ 0, 0, Wid, Ht },	/* clipr */
	Depth,			/* depth */
	3,			/* nchan */
	RGB16,			/* chan */
	nil,			/* cmap */
	&xgdata,		/* data */
	0,			/* zero */
	0, 			/* width in words of a single scan line */
	0,			/* layer */
	0,			/* flags */
};

static Memimage *conscol;
static Memimage *back;
static Memsubfont *memdefont;

static Lock screenlock;

static Point	curpos;
static int	h, w;
static Rectangle window;

static void myscreenputs(char *s, int n);
static void screenputc(char *buf);
static void screenwin(void);

/*
 * Software cursor. 
 */
static int	swvisible;	/* is the cursor visible? */
static int	swenabled;	/* is the cursor supposed to be on the screen? */
static Memimage *swback;	/* screen under cursor */
static Memimage *swimg;		/* cursor image */
static Memimage *swmask;	/* cursor mask */
static Memimage *swimg1;
static Memimage *swmask1;

static Point	swoffset;
static Rectangle swrect;	/* screen rectangle in swback */
static Point	swpt;		/* desired cursor location */
static Point	swvispt;	/* actual cursor location */
static int	swvers;		/* incremented each time cursor image changes */
static int	swvisvers;	/* the version on the screen */

/*
 * called with drawlock locked for us, most of the time.
 * kernel prints at inopportune times might mean we don't
 * hold the lock, but memimagedraw is now reentrant so
 * that should be okay: worst case we get cursor droppings.
 */
static void
swcursorhide(void)
{
	if(swvisible == 0)
		return;
	if(swback == nil)
		return;
	swvisible = 0;
	memimagedraw(gscreen, swrect, swback, ZP, memopaque, ZP, S);
	flushmemscreen(swrect);
}

static void
swcursoravoid(Rectangle r)
{
	if(swvisible && rectXrect(r, swrect))
		swcursorhide();
}

static void
swcursordraw(void)
{
	int dounlock;

	if(swvisible)
		return;
	if(swenabled == 0)
		return;
	if(swback == nil || swimg1 == nil || swmask1 == nil)
		return;
	dounlock = canqlock(&drawlock);
	swvispt = swpt;
	swvisvers = swvers;
	swrect = rectaddpt(Rect(0,0,16,16), swvispt);
	memimagedraw(swback, swback->r, gscreen, swpt, memopaque, ZP, S);
	memimagedraw(gscreen, swrect, swimg1, ZP, swmask1, ZP, SoverD);
	flushmemscreen(swrect);
	swvisible = 1;
	if(dounlock)
		qunlock(&drawlock);
}

int
cursoron(int dolock)
{
	int retry;

	if (dolock)
		lock(&cursor);
	if (canqlock(&drawlock)) {
		retry = 0;
		swcursorhide();
		swcursordraw();
		qunlock(&drawlock);
	} else
		retry = 1;
	if (dolock)
		unlock(&cursor);
	return retry;
}

void
cursoroff(int dolock)
{
	if (dolock)
		lock(&cursor);
	swcursorhide();
	if (dolock)
		unlock(&cursor);
}

static void
swload(Cursor *curs)
{
	uchar *ip, *mp;
	int i, j, set, clr;

	if(!swimg || !swmask || !swimg1 || !swmask1)
		return;
	/*
	 * Build cursor image and mask.
	 * Image is just the usual cursor image
	 * but mask is a transparent alpha mask.
	 * 
	 * The 16x16x8 memimages do not have
	 * padding at the end of their scan lines.
	 */
	ip = byteaddr(swimg, ZP);
	mp = byteaddr(swmask, ZP);
	for(i=0; i<32; i++){
		set = curs->set[i];
		clr = curs->clr[i];
		for(j=0x80; j; j>>=1){
			*ip++ = set&j ? 0x00 : 0xFF;
			*mp++ = (clr|set)&j ? 0xFF : 0x00;
		}
	}
	swoffset = curs->offset;
	swvers++;
	memimagedraw(swimg1,  swimg1->r,  swimg,  ZP, memopaque, ZP, S);
	memimagedraw(swmask1, swmask1->r, swmask, ZP, memopaque, ZP, S);
}

/* called from devmouse */
void
setcursor(Cursor* curs)
{
	cursoroff(0);
	swload(curs);
	cursoron(0);
}

static int
swmove(Point p)
{
	swpt = addpt(p, swoffset);
	return 0;
}

static void
swcursorclock(void)
{
	int x;

	if(!swenabled)
		return;
	swmove(mousexy());
	if(swvisible && eqpt(swpt, swvispt) && swvers==swvisvers)
		return;

	x = splhi();
	if(swenabled)
	if(!swvisible || !eqpt(swpt, swvispt) || swvers!=swvisvers)
	if(canqlock(&drawlock)){
		swcursorhide();
		swcursordraw();
		qunlock(&drawlock);
	}
	splx(x);
}

void
swcursorinit(void)
{
	static int init;

	if(!init){
		init = 1;
		addclock0link(swcursorclock, 10);
		swenabled = 1;
	}
	if(swback){
		freememimage(swback);
		freememimage(swmask);
		freememimage(swmask1);
		freememimage(swimg);
		freememimage(swimg1);
	}

	swback  = allocmemimage(Rect(0,0,32,32), gscreen->chan);
	swmask  = allocmemimage(Rect(0,0,16,16), GREY8);
	swmask1 = allocmemimage(Rect(0,0,16,16), GREY1);
	swimg   = allocmemimage(Rect(0,0,16,16), GREY8);
	swimg1  = allocmemimage(Rect(0,0,16,16), GREY1);
	if(swback==nil || swmask==nil || swmask1==nil || swimg==nil || swimg1 == nil){
		print("software cursor: allocmemimage fails\n");
		return;
	}

	memfillcolor(swmask, DOpaque);
	memfillcolor(swmask1, DOpaque);
	memfillcolor(swimg, DBlack);
	memfillcolor(swimg1, DBlack);
}

int
hwdraw(Memdrawparam *par)
{
	Memimage *dst, *src, *mask;

	if((dst=par->dst) == nil || dst->data == nil)
		return 0;
	if((src=par->src) == nil || src->data == nil)
		return 0;
	if((mask=par->mask) == nil || mask->data == nil)
		return 0;

	if(dst->data->bdata == xgdata.bdata)
		swcursoravoid(par->r);
	if(src->data->bdata == xgdata.bdata)
		swcursoravoid(par->sr);
	if(mask->data->bdata == xgdata.bdata)
		swcursoravoid(par->mr);

	if(lcd)
		lcd->draw(par->r);

	return 0;
}

static int
screensize(void)
{
	char *p, buf[32];
	char *f[3];
	int width, height, depth;

	p = getconf("vgasize");
	if(p == nil || memccpy(buf, p, '\0', sizeof buf) == nil)
		return -1;

	if(getfields(buf, f, nelem(f), 0, "x") != nelem(f) ||
	    (width = atoi(f[0])) < 16 ||
	    (height = atoi(f[1])) <= 0 ||
	    (depth = atoi(f[2])) <= 0)
		return -1;
	xgscreen.r.max = Pt(width, height);
	xgscreen.depth = depth;
	return 0;
}

void
screeninit(void)
{
	uchar *fb;
	char *p;
	int set, rgbswap;
	ulong chan;

	set = screensize() == 0;
	fb = fbinit(set, &xgscreen.r.max.x, &xgscreen.r.max.y, &xgscreen.depth);
	if(fb == nil){
		print("can't initialise %dx%dx%d framebuffer \n",
			xgscreen.r.max.x, xgscreen.r.max.y, xgscreen.depth);
		return;
	}
	rgbswap = ((p = getconf("bcm2708_fb.fbswap")) != nil && *p == '1');
	xgscreen.clipr = xgscreen.r;
	switch(xgscreen.depth){
	default:
		print("unsupported screen depth %d\n", xgscreen.depth);
		xgscreen.depth = 16;
		/* fall through */
	case 16:
		chan = RGB16;
		break;
	case 24:
		chan = rgbswap? RGB24 : BGR24;
		break;
	case 32:
		chan = ARGB32;
		break;
	}
	memsetchan(&xgscreen, chan);
	conf.monitor = 1;
	xgdata.bdata = fb;
	xgdata.ref = 1;
	gscreen = &xgscreen;
	gscreen->width = wordsperline(gscreen->r, gscreen->depth);

	memimageinit();
	memdefont = getmemdefont();
	screenwin();
	screenputs = myscreenputs;
}

void
flushmemscreen(Rectangle)
{
}

uchar*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;
	*softscreen = 0;

	return gscreen->data->bdata;
}

void
getcolor(ulong p, ulong *pr, ulong *pg, ulong *pb)
{
	USED(p, pr, pg, pb);
}

int
setcolor(ulong p, ulong r, ulong g, ulong b)
{
	USED(p, r, g, b);
	return 0;
}

void
blankscreen(int blank)
{
	fbblank(blank);
	if(lcd)
		lcd->blank(blank);
}

static void
myscreenputs(char *s, int n)
{
	int i;
	Rune r;
	char buf[4];

	if(!islo()) {
		/* don't deadlock trying to print in interrupt */
		if(!canlock(&screenlock))
			return;	
	}
	else
		lock(&screenlock);

	while(n > 0){
		i = chartorune(&r, s);
		if(i == 0){
			s++;
			--n;
			continue;
		}
		memmove(buf, s, i);
		buf[i] = 0;
		n -= i;
		s += i;
		screenputc(buf);
	}
	unlock(&screenlock);
}

static void
screenwin(void)
{
	char *greet;
	Memimage *orange;
	Point p, q;
	Rectangle r;

	back = memwhite;
	conscol = memblack;

	orange = allocmemimage(Rect(0, 0, 1, 1), RGB16);
	orange->flags |= Frepl;
	orange->clipr = gscreen->r;
	orange->data->bdata[0] = 0x40;		/* magic: colour? */
	orange->data->bdata[1] = 0xfd;		/* magic: colour? */

	w = memdefont->info[' '].width;
	h = memdefont->height;

	r = insetrect(gscreen->r, 4);

	memimagedraw(gscreen, r, memblack, ZP, memopaque, ZP, S);
	window = insetrect(r, 4);
	memimagedraw(gscreen, window, memwhite, ZP, memopaque, ZP, S);

	memimagedraw(gscreen, Rect(window.min.x, window.min.y,
		window.max.x, window.min.y + h + 5 + 6), orange, ZP, nil, ZP, S);
	freememimage(orange);
	window = insetrect(window, 5);

	greet = " Plan 9 Console ";
	p = addpt(window.min, Pt(10, 0));
	q = memsubfontwidth(memdefont, greet);
	memimagestring(gscreen, p, conscol, ZP, memdefont, greet);
	flushmemscreen(r);
	window.min.y += h + 6;
	curpos = window.min;
	window.max.y = window.min.y + ((window.max.y - window.min.y) / h) * h;
}

static void
scroll(void)
{
	int o;
	Point p;
	Rectangle r;

	o = Scroll*h;
	r = Rpt(window.min, Pt(window.max.x, window.max.y-o));
	p = Pt(window.min.x, window.min.y+o);
	memimagedraw(gscreen, r, gscreen, p, nil, p, S);
	flushmemscreen(r);
	if(lcd)
		lcd->draw(r);
	r = Rpt(Pt(window.min.x, window.max.y-o), window.max);
	memimagedraw(gscreen, r, back, ZP, nil, ZP, S);
	flushmemscreen(r);
	if(lcd)
		lcd->draw(r);

	curpos.y -= o;
}

static void
screenputc(char *buf)
{
	int w;
	uint pos;
	Point p;
	Rectangle r;
	static int *xp;
	static int xbuf[256];

	if (xp < xbuf || xp >= &xbuf[sizeof(xbuf)])
		xp = xbuf;

	switch (buf[0]) {
	case '\n':
		if (curpos.y + h >= window.max.y)
			scroll();
		curpos.y += h;
		screenputc("\r");
		break;
	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;
	case '\t':
		p = memsubfontwidth(memdefont, " ");
		w = p.x;
		if (curpos.x >= window.max.x - Tabstop * w)
			screenputc("\n");

		pos = (curpos.x - window.min.x) / w;
		pos = Tabstop - pos % Tabstop;
		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x + pos * w, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		flushmemscreen(r);
		curpos.x += pos * w;
		break;
	case '\b':
		if (xp <= xbuf)
			break;
		xp--;
		r = Rect(*xp, curpos.y, curpos.x, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		flushmemscreen(r);
		curpos.x = *xp;
		break;
	case '\0':
		break;
	default:
		p = memsubfontwidth(memdefont, buf);
		w = p.x;

		if (curpos.x >= window.max.x - w)
			screenputc("\n");

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x + w, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		memimagestring(gscreen, curpos, conscol, ZP, memdefont, buf);
		flushmemscreen(r);
		curpos.x += w;
		break;
	}
}
*/
	Depth,			/* depth */
	3,			/* nchan */
	RGB16,			/* chan */
	nil,			/* cmap */
	&xgdata,		/* data */
	0,			/* zero */
	0, 	screen.h                                                                                               664       0       0         1565 12677465530  10670                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    typedef struct Cursor Cursor;
typedef struct Cursorinfo Cursorinfo;
typedef struct Lcd Lcd;

struct Cursorinfo {
	Cursor;
	Lock;
};

struct Lcd {
	void	(*draw)(Rectangle);
	void	(*blank)(int);
};

/* devmouse.c */
extern void mousetrack(int, int, int, int);
extern Point mousexy(void);

extern void mouseaccelerate(int);
extern int m3mouseputc(Queue*, int);
extern int m5mouseputc(Queue*, int);
extern int mouseputc(Queue*, int);

extern Cursorinfo cursor;
extern Cursor arrow;

/* mouse.c */
extern void mousectl(Cmdbuf*);
extern void mouseresize(void);

/* screen.c */
extern void	blankscreen(int);
extern void	flushmemscreen(Rectangle);
extern uchar*	attachscreen(Rectangle*, ulong*, int*, int*, int*);
extern int	cursoron(int);
extern void	cursoroff(int);
extern void	setcursor(Cursor*);

/* devdraw.c */
extern QLock	drawlock;

#define ishwimage(i)	1		/* for ../port/devdraw.c */
                                                                                                                                           sdhc.c                                                                                                 664       0       0        27375 13530307552  10340                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2711 sd host controller
 *
 * Copyright © 2012,2019 Richard Miller <r.miller@acm.org>
 *
 * adapted from emmc.c - the two should really be merged
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

extern SDio *sdcardlink;
#define EMMCREGS	(VIRTIO+0x340000)
#define ClkEmmc2 12

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
	SDMAaddr		= 0x00>>2,
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
	Capability		= 0x40>>2,
	Forceirpt		= 0x50>>2,
	Dmadesc			= 0x58>>2,
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
	Busvoltage		= 7<<9,
		V1_8		= 5<<9,
		V3_0		= 6<<9,
		V3_3		= 7<<9,
	Buspower		= 1<<8,
	Dwidth8			= 1<<5,
	Dmaselect		= 3<<3,
		DmaSDMA		= 0<<3,
		DmaADMA1	= 1<<3,
		DmaADMA2	= 2<<3,
	Hispeed			= 1<<2,
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,
	LED			= 1<<0,

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
	Dmaen			= 1<<0,

	/* Interrupt */
	Admaerr		= 1<<25,
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
	Dmaintr		= 1<<3,
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

typedef struct Adma Adma;
typedef struct Ctlr Ctlr;

/*
 * ADMA2 descriptor
 *	See SD Host Controller Simplified Specification Version 2.00
 */

struct Adma {
	u32int	desc;
	u32int	addr;
};

enum {
	/* desc fields */
	Valid		= 1<<0,
	End			= 1<<1,
	Int			= 1<<2,
	Nop			= 0<<4,
	Tran		= 2<<4,
	Link		= 3<<4,
	OLength		= 16,
	/* maximum value for Length field */
	Maxdma		= ((1<<16) - 4),
};

struct Ctlr {
	Rendez	r;
	Rendez	cardr;
	int	fastclock;
	ulong	extclk;
	int	appcmd;
	Adma	*dma;
};

static Ctlr emmc;

static void mmcinterrupt(Ureg*, void*);

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)EMMCREGS;

	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	coherence();
	r[reg] = val;
}

static uint
clkdiv(uint d)
{
	uint v;

	assert(d < 1<<10);
	v = (d << Clkfreq8shift) & Clkfreq8mask;
	v |= ((d >> 8) << Clkfreqms2shift) & Clkfreqms2mask;
	return v;
}

static Adma*
dmaalloc(void *addr, int len)
{
	int n;
	uintptr a;
	Adma *adma, *p;

	a = (uintptr)addr;
	n = HOWMANY(len, Maxdma);
	adma = sdmalloc(n * sizeof(Adma));
	for(p = adma; len > 0; p++){
		p->desc = Valid | Tran;
		if(n == 1)
			p->desc |= len<<OLength | End | Int;
		else
			p->desc |= Maxdma<<OLength;
		p->addr = dmaaddr((void*)a);
		a += Maxdma;
		len -= Maxdma;
		n--;
	}
	cachedwbse(adma, (char*)p - (char*)adma);
	return adma;
}

static void
emmcclk(uint freq)
{
	u32int *r;
	uint div;
	int i;

	r = (u32int*)EMMCREGS;
	div = emmc.extclk / (freq<<1);
	if(emmc.extclk / (div<<1) > freq)
		div++;
	WR(Control1, clkdiv(div) |
		DTO<<Datatoshift | Clkgendiv | Clken | Clkintlen);
	for(i = 0; i < 1000; i++){
		delay(1);
		if(r[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("sdhc: can't set clock to %ud\n", freq);
}

static int
datadone(void*)
{
	int i;

	u32int *r = (u32int*)EMMCREGS;
	i = r[Interrupt];
	return i & (Datadone|Err);
}

static int
emmcinit(void)
{
	u32int *r;
	ulong clk;

	clk = getclkrate(ClkEmmc2);
	if(clk == 0){
		clk = Extfreq;
		print("sdhc: assuming external clock %lud Mhz\n", clk/1000000);
	}
	emmc.extclk = clk;
	r = (u32int*)EMMCREGS;
	if(0)print("sdhc control %8.8ux %8.8ux %8.8ux\n",
		r[Control0], r[Control1], r[Control2]);
	WR(Control1, Srsthc);
	delay(10);
	while(r[Control1] & Srsthc)
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

	r = (u32int*)EMMCREGS;
	ver = r[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
		"BCM SD Host Controller %2.2x Version %2.2x",
		ver&0xFF, ver>>8);
}

static void
emmcenable(void)
{

	WR(Control0, 0);
	delay(1);
	WR(Control0, V3_3 | Buspower | Dwidth1 | DmaADMA2);
	WR(Control1, 0);
	delay(1);
	emmcclk(Initfreq);
	WR(Irpten, 0);
	WR(Irptmask, ~(Cardintr|Dmaintr));
	WR(Interrupt, ~0);
	intrenable(IRQmmc, mmcinterrupt, nil, 0, "sdhc");
}

static int
emmccmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i;
	ulong now;

	r = (u32int*)EMMCREGS;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if(cmd == Switchfunc && !emmc.appcmd)
		c |= Isdata|Card2host;
	if(c & Isdata)
		c |= Dmaen;
	if(cmd == IORWextended){
		if(arg & (1<<31))
			c |= Host2card;
		else
			c |= Card2host;
		if((r[Blksizecnt]&0xFFFF0000) != 0x10000)
			c |= Multiblock | Blkcnten;
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if(cmd == GoIdle){
		WR(Control0, r[Control0] & ~(Dwidth4|Hispeed));
		emmcclk(Initfreq);
	}
	if(r[Status] & Cmdinhibit){
		print("sdhc: need to reset Cmdinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((r[Status] & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("sdhc: need to reset Datinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	WR(Arg1, arg);
	if((i = (r[Interrupt] & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("sdhc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = m->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if(m->ticks-now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr)
			print("sdhc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, r[Status]);
		WR(Interrupt, i);
		if(r[Status]&Cmdinhibit){
			WR(Control1, r[Control1]|Srstcmd);
			while(r[Control1]&Srstcmd)
				;
		}
		error(Eio);
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
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		WR(Irpten, r[Irpten]|Datadone|Err);
		tsleep(&emmc.r, datadone, 0, 3000);
		i = r[Interrupt];
		if((i & Datadone) == 0)
			print("sdhc: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			print("sdhc: CMD%d error interrupt %ux\n",
				cmd, r[Interrupt]);
		WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		delay(1);
		emmcclk(SDfreq);
		delay(1);
		emmc.fastclock = 1;
	}
	if(cmd == Setbuswidth){
		if(emmc.appcmd){
			/*
			 * If card bus width changes, change host bus width
			 */
			switch(arg){
			case 0:
				WR(Control0, r[Control0] & ~Dwidth4);
				break;
			case 2:
				WR(Control0, r[Control0] | Dwidth4);
				break;
			}
		}else{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				emmcclk(SDfreqhs);
				delay(1);
			}
		}
	}else if(cmd == IORWdirect && (arg & ~0xFF) == (1<<31|0<<28|7<<9)){
		switch(arg & 0x3){
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

static void
emmciosetup(int write, void *buf, int bsize, int bcount)
{
	int len;

	len = bsize * bcount;
	assert(((uintptr)buf&3) == 0);
	assert((len&3) == 0);
	assert(bsize <= 2048);
	WR(Blksizecnt, bcount<<16 | bsize);
	if(emmc.dma)
		sdfree(emmc.dma);
	emmc.dma = dmaalloc(buf, len);
	if(write)
		cachedwbse(buf, len);
	else
		cachedwbinvse(buf, len);
	WR(Dmadesc, dmaaddr(emmc.dma));
	okay(1);
}

static void
emmcio(int write, uchar *buf, int len)
{
	u32int *r;
	int i;

	r = (u32int*)EMMCREGS;
	if(waserror()){
		okay(0);
		nexterror();
	}
	WR(Irpten, r[Irpten] | Datadone|Err);
	tsleep(&emmc.r, datadone, 0, 3000);
	WR(Irpten, r[Irpten] & ~(Datadone|Err));
	i = r[Interrupt];
	if((i & (Datadone|Err)) != Datadone){
		print("sdhc: %s error intr %ux stat %ux\n",
			write? "write" : "read", i, r[Status]);
		if(r[Status] & Datinhibit)
			WR(Control1, r[Control1] | Srstdata);
			while(r[Control1] & Srstdata)
				;
			while(r[Status] & Datinhibit)
				;
		WR(Interrupt, i);
		error(Eio);
	}
	WR(Interrupt, i);
	if(!write)
		cachedinvse(buf, len);
	poperror();
	okay(0);
}

static void
mmcinterrupt(Ureg*, void*)
{	
	u32int *r;
	int i;

	r = (u32int*)EMMCREGS;
	i = r[Interrupt];
	if(i&(Datadone|Err))
		wakeup(&emmc.r);
	if(i&Cardintr)
		wakeup(&emmc.cardr);
	WR(Irpten, r[Irpten] & ~i);
}

SDio sdiohc = {
	"sdhc",
	emmcinit,
	emmcenable,
	emmcinquiry,
	emmccmd,
	emmciosetup,
	emmcio,
};

void
sdhclink(void)
{
	sdcardlink = &sdiohc;
}
 0xfc>>2,

	/* Control0 */
	Busvoltage		= 7<<9,
		V1_8		= 5<<9,
		V3_0		= 6<<9,
		V3_3		= 7<<9,
	Buspower		= 1<<8,
	Dwidth8			= 1<<5,
	Dmaselect		= 3<<3,
		DmaSDMA		= 0<<3,
		DmaADMA1	= 1<<3,
		DmaADMA2	= 2<<3,
	Hispeed			= 1<<2,
	Dwidth4			= 1<<1,
	Dwidth1		sdhost.c                                                                                               664       0       0        16452 13261216401  10706                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2835 sdhost controller
 *
 * Copyright © 2016 Richard Miller <r.miller@acm.org>
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

extern SDio *sdcardlink;

#define SDHOSTREGS	(VIRTIO+0x202000)

enum {
	Extfreq		= 250*Mhz,	/* guess external clock frequency if vcore doesn't say */
	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25*Mhz,	/* standard SD frequency */
	SDfreqhs	= 50*Mhz,	/* SD high speed frequency */
	FifoDepth	= 4,		/* "Limit fifo usage due to silicon bug" (linux driver) */

	GoIdle		= 0,		/* mmc/sdio go idle state */
	MMCSelect	= 7,		/* mmc/sd card select command */
	Setbuswidth	= 6,		/* mmc/sd set bus width command */
	Switchfunc	= 6,		/* mmc/sd switch function command */
	Stoptransmission = 12,	/* mmc/sd stop transmission command */
	Appcmd = 55,			/* mmc/sd application command prefix */
};

enum {
	/* Controller registers */
	Cmd		= 0x00>>2,
	Arg		= 0x04>>2,
	Timeout		= 0x08>>2,
	Clkdiv		= 0x0c>>2,

	Resp0		= 0x10>>2,
	Resp1		= 0x14>>2,
	Resp2		= 0x18>>2,
	Resp3		= 0x1c>>2,

	Status		= 0x20>>2,
	Poweron		= 0x30>>2,
	Dbgmode		= 0x34>>2,
	Hconfig		= 0x38>>2,
	Blksize		= 0x3c>>2,
	Data		= 0x40>>2,
	Blkcount	= 0x50>>2,

	/* Cmd */
	Start		= 1<<15,
	Failed		= 1<<14,
	Respmask	= 7<<9,
	Resp48busy	= 4<<9,
	Respnone	= 2<<9,
	Resp136		= 1<<9,
	Resp48		= 0<<9,
	Host2card	= 1<<7,
	Card2host	= 1<<6,

	/* Status */
	Busyint		= 1<<10,
	Blkint		= 1<<9,
	Sdioint		= 1<<8,
	Rewtimeout	= 1<<7,
	Cmdtimeout	= 1<<6,
	Crcerror	= 3<<4,
	Fifoerror	= 1<<3,
	Dataflag	= 1<<0,
	Intstatus	= (Busyint|Blkint|Sdioint|Dataflag),
	Errstatus	= (Rewtimeout|Cmdtimeout|Crcerror|Fifoerror),

	/* Hconfig */
	BusyintEn	= 1<<10,
	BlkintEn	= 1<<8,
	SdiointEn	= 1<<5,
	DataintEn	= 1<<4,
	Slowcard	= 1<<3,
	Extbus4		= 1<<2,
	Intbuswide	= 1<<1,
	Cmdrelease	= 1<<0,
};

static int cmdinfo[64] = {
[0]  Start | Respnone,
[2]  Start | Resp136,
[3]  Start | Resp48,
[5]  Start | Resp48,
[6]  Start | Resp48,
[63]  Start | Resp48 | Card2host,
[7]  Start | Resp48busy,
[8]  Start | Resp48,
[9]  Start | Resp136,
[11] Start | Resp48,
[12] Start | Resp48busy,
[13] Start | Resp48,
[16] Start | Resp48,
[17] Start | Resp48 | Card2host,
[18] Start | Resp48 | Card2host,
[24] Start | Resp48 | Host2card,
[25] Start | Resp48 | Host2card,
[41] Start | Resp48,
[52] Start | Resp48,
[53] Start | Resp48,
[55] Start | Resp48,
};

typedef struct Ctlr Ctlr;

struct Ctlr {
	Rendez	r;
	int	bcount;
	int	done;
	ulong	extclk;
	int	appcmd;
};

static Ctlr sdhost;

static void sdhostinterrupt(Ureg*, void*);

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)SDHOSTREGS;

	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	r[reg] = val;
}

static int
datadone(void*)
{
	return sdhost.done;
}

static void
sdhostclock(uint freq)
{
	uint div;

	div = sdhost.extclk / freq;
	if(sdhost.extclk / freq > freq)
		div++;
	if(div < 2)
		div = 2;
	WR(Clkdiv, div - 2);
}

static int
sdhostinit(void)
{
	u32int *r;
	ulong clk;
	int i;

	/* disconnect emmc and connect sdhost to SD card gpio pins */
	for(i = 48; i <= 53; i++)
		gpiosel(i, Alt0);
	clk = getclkrate(ClkCore);
	if(clk == 0){
		clk = Extfreq;
		print("sdhost: assuming external clock %lud Mhz\n", clk/1000000);
	}
	sdhost.extclk = clk;
	sdhostclock(Initfreq);
	r = (u32int*)SDHOSTREGS;
	WR(Poweron, 0);
	WR(Timeout, 0xF00000);
	WR(Dbgmode, FINS(r[Dbgmode], 9, 10, (FifoDepth | FifoDepth<<5)));
	return 0;
}

static int
sdhostinquiry(char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "BCM SDHost Controller");
}

static void
sdhostenable(void)
{
	u32int *r;

	r = (u32int*)SDHOSTREGS;
	USED(r);
	WR(Poweron, 1);
	delay(10);
	WR(Hconfig, Intbuswide | Slowcard | BusyintEn);
	WR(Clkdiv, 0x7FF);
	intrenable(IRQsdhost, sdhostinterrupt, nil, 0, "sdhost");
}

static int
sdhostcmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i;
	ulong now;

	r = (u32int*)SDHOSTREGS;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = cmd | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if(cmd == Switchfunc && !sdhost.appcmd)
		c |= Card2host;
	if(cmd != Stoptransmission && (i = (r[Dbgmode] & 0xF)) > 2){
		print("sdhost: previous command stuck: Dbg=%d Cmd=%ux\n", i, r[Cmd]);
		error(Eio);
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if(cmd == GoIdle){
		WR(Hconfig, r[Hconfig] & ~Extbus4);
		sdhostclock(Initfreq);
	}

	if(r[Status] & (Errstatus|Dataflag))
		WR(Status, Errstatus|Dataflag);
	sdhost.done = 0;
	WR(Arg, arg);
	WR(Cmd, c);
	now = m->ticks;
	while(((i=r[Cmd])&(Start|Failed)) == Start)
		if(m->ticks-now > HZ)
			break;
	if((i&(Start|Failed)) != 0){
		if(r[Status] != Cmdtimeout)
			print("sdhost: cmd %ux arg %ux error stat %ux\n", i, arg, r[Status]);
		i = r[Status];
		WR(Status, i);
		error(Eio);
	}
	switch(c & Respmask){
	case Resp136:
		resp[0] = r[Resp0];
		resp[1] = r[Resp1];
		resp[2] = r[Resp2];
		resp[3] = r[Resp3];
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		tsleep(&sdhost.r, datadone, 0, 3000);
	}
	switch(cmd) {
	case MMCSelect:
		/*
		 * Once card is selected, use faster clock
		 */
		delay(1);
		sdhostclock(SDfreq);
		delay(1);
		break;
	case Setbuswidth:
		if(sdhost.appcmd){
			/*
			 * If card bus width changes, change host bus width
			 */
			switch(arg){
			case 0:
				WR(Hconfig, r[Hconfig] & ~Extbus4);
				break;
			case 2:
				WR(Hconfig, r[Hconfig] | Extbus4);
				break;
			}
		}else{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				sdhostclock(SDfreqhs);
				delay(1);
			}
		}
		break;
	}
	sdhost.appcmd = (cmd == Appcmd);
	return 0;
}

void
sdhostiosetup(int write, void *buf, int bsize, int bcount)
{
	USED(write);
	USED(buf);

	sdhost.bcount = bcount;
	WR(Blksize, bsize);
	WR(Blkcount, bcount);
}

static void
sdhostio(int write, uchar *buf, int len)
{
	u32int *r;
	int piolen;
	u32int w;

	r = (u32int*)SDHOSTREGS;
	assert((len&3) == 0);
	assert((PTR2UINT(buf)&3) == 0);
	okay(1);
	if(waserror()){
		okay(0);
		nexterror();
	}
	/*
	 * According to comments in the linux driver, the hardware "doesn't
	 * manage the FIFO DREQs properly for multi-block transfers" on input,
	 * so the dma must be stopped early and the last 3 words fetched with pio
	 */
	piolen = 0;
	if(!write && sdhost.bcount > 1){
		piolen = (FifoDepth-1) * sizeof(u32int);
		len -= piolen;
	}
	if(write)
		dmastart(DmaChanSdhost, DmaDevSdhost, DmaM2D,
			buf, &r[Data], len);
	else
		dmastart(DmaChanSdhost, DmaDevSdhost, DmaD2M,
			&r[Data], buf, len);
	if(dmawait(DmaChanSdhost) < 0)
		error(Eio);
	if(!write){
		cachedinvse(buf, len);
		buf += len;
		for(; piolen > 0; piolen -= sizeof(u32int)){
			if((r[Dbgmode] & 0x1F00) == 0){
				print("sdhost: FIFO empty after short dma read\n");
				error(Eio);
			}
			w = r[Data];
			*((u32int*)buf) = w;
			buf += sizeof(u32int);
		}
	}
	poperror();
	okay(0);
}

static void
sdhostinterrupt(Ureg*, void*)
{	
	u32int *r;
	int i;

	r = (u32int*)SDHOSTREGS;
	i = r[Status];
	WR(Status, i);
	if(i & Busyint){
		sdhost.done = 1;
		wakeup(&sdhost.r);
	}
}

SDio sdiohost = {
	"sdhost",
	sdhostinit,
	sdhostenable,
	sdhostinquiry,
	sdhostcmd,
	sdhostiosetup,
	sdhostio,
};

void
sdhostlink(void)
{
	sdcardlink = &sdiohost;
}
			case 2:
				WR(Control0, r[Control0] | Dwidth4);
				break;
			}
		}else{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				esdmmc.c                                                                                                664       0       0        14502 13256731445  10515                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * mmc / sd memory card
 *
 * Copyright © 2012 Richard Miller <r.miller@acm.org>
 *
 * Assumes only one card on the bus
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "../port/sd.h"

#define CSD(end, start)	rbits(csd, start, (end)-(start)+1)

typedef struct Ctlr Ctlr;

enum {
	Inittimeout	= 15,
	Multiblock	= 1,

	/* Commands */
	GO_IDLE_STATE	= 0,
	ALL_SEND_CID	= 2,
	SEND_RELATIVE_ADDR= 3,
	SWITCH_FUNC	= 6,
	SELECT_CARD	= 7,
	SD_SEND_IF_COND	= 8,
	SEND_CSD	= 9,
	STOP_TRANSMISSION= 12,
	SEND_STATUS	= 13,
	SET_BLOCKLEN	= 16,
	READ_SINGLE_BLOCK= 17,
	READ_MULTIPLE_BLOCK= 18,
	WRITE_BLOCK	= 24,
	WRITE_MULTIPLE_BLOCK= 25,
	APP_CMD		= 55,	/* prefix for following app-specific commands */
	SET_BUS_WIDTH	= 6,
	SD_SEND_OP_COND	= 41,

	/* Command arguments */
	/* SD_SEND_IF_COND */
	Voltage		= 1<<8,
	Checkpattern	= 0x42,

	/* SELECT_CARD */
	Rcashift	= 16,

	/* SD_SEND_OP_COND */
	Hcs	= 1<<30,	/* host supports SDHC & SDXC */
	Ccs	= 1<<30,	/* card is SDHC or SDXC */
	V3_3	= 3<<20,	/* 3.2-3.4 volts */

	/* SET_BUS_WIDTH */
	Width1	= 0<<0,
	Width4	= 2<<0,

	/* SWITCH_FUNC */
	Dfltspeed	= 0<<0,
	Hispeed		= 1<<0,
	Checkfunc	= 0x00FFFFF0,
	Setfunc		= 0x80FFFFF0,
	Funcbytes	= 64,

	/* OCR (operating conditions register) */
	Powerup	= 1<<31,
};

struct Ctlr {
	SDev	*dev;
	SDio	*io;
	/* SD card registers */
	u16int	rca;
	u32int	ocr;
	u32int	cid[4];
	u32int	csd[4];
};

SDio *sdcardlink;

extern SDifc sdmmcifc;

static uint
rbits(u32int *p, uint start, uint len)
{
	uint w, off, v;

	w   = start / 32;
	off = start % 32;
	if(off == 0)
		v = p[w];
	else
		v = p[w] >> off | p[w+1] << (32-off);
	if(len < 32)
		return v & ((1<<len) - 1);
	else
		return v;
}

static void
identify(SDunit *unit, u32int *csd)
{
	uint csize, mult;

	unit->secsize = 1 << CSD(83, 80);
	switch(CSD(127, 126)){
	case 0:				/* CSD version 1 */
		csize = CSD(73, 62);
		mult = CSD(49, 47);
		unit->sectors = (csize+1) * (1<<(mult+2));
		break;
	case 1:				/* CSD version 2 */
		csize = CSD(69, 48);
		unit->sectors = (csize+1) * 512LL*KiB / unit->secsize;
		break;
	}
	if(unit->secsize == 1024){
		unit->sectors <<= 1;
		unit->secsize = 512;
	}
}

static SDev*
mmcpnp(void)
{
	SDev *sdev;
	Ctlr *ctl;

	if(sdcardlink == nil)
		sdcardlink = &sdio;
	if(sdcardlink->init() < 0)
		return nil;
	sdev = malloc(sizeof(SDev));
	if(sdev == nil)
		return nil;
	ctl = malloc(sizeof(Ctlr));
	if(ctl == nil){
		free(sdev);
		return nil;
	}
	sdev->idno = 'M';
	sdev->ifc = &sdmmcifc;
	sdev->nunit = 1;
	sdev->ctlr = ctl;
	ctl->dev = sdev;
	ctl->io = sdcardlink;
	return sdev;
}

static int
mmcverify(SDunit *unit)
{
	int n;
	Ctlr *ctl;

	ctl = unit->dev->ctlr;
	n = ctl->io->inquiry((char*)&unit->inquiry[8], sizeof(unit->inquiry)-8);
	if(n < 0)
		return 0;
	unit->inquiry[0] = SDperdisk;
	unit->inquiry[1] = SDinq1removable;
	unit->inquiry[4] = sizeof(unit->inquiry)-4;
	return 1;
}

static int
mmcenable(SDev* dev)
{
	Ctlr *ctl;

	ctl = dev->ctlr;
	ctl->io->enable();
	return 1;
}

static void
mmcswitchfunc(SDio *io, int arg)
{
	uchar *buf;
	int n;
	u32int r[4];

	n = Funcbytes;
	buf = sdmalloc(n);
	if(waserror()){
		print("mmcswitchfunc error\n");
		sdfree(buf);
		nexterror();
	}
	io->iosetup(0, buf, n, 1);
	io->cmd(SWITCH_FUNC, arg, r);
	io->io(0, buf, n);
	sdfree(buf);
	poperror();
}

static int
mmconline(SDunit *unit)
{
	int hcs, i;
	u32int r[4];
	Ctlr *ctl;
	SDio *io;

	ctl = unit->dev->ctlr;
	io = ctl->io;
	assert(unit->subno == 0);

	if(waserror()){
		unit->sectors = 0;
		return 0;
	}
	if(unit->sectors != 0){
		io->cmd(SEND_STATUS, ctl->rca<<Rcashift, r);
		poperror();
		return 1;
	}
	io->cmd(GO_IDLE_STATE, 0, r);
	hcs = 0;
	if(!waserror()){
		io->cmd(SD_SEND_IF_COND, Voltage|Checkpattern, r);
		if(r[0] == (Voltage|Checkpattern))	/* SD 2.0 or above */
			hcs = Hcs;
		poperror();
	}
	for(i = 0; i < Inittimeout; i++){
		delay(100);
		io->cmd(APP_CMD, 0, r);
		io->cmd(SD_SEND_OP_COND, hcs|V3_3, r);
		if(r[0] & Powerup)
			break;
	}
	if(i == Inittimeout){
		print("sdmmc: card won't power up\n");
		poperror();
		return 2;
	}
	ctl->ocr = r[0];
	io->cmd(ALL_SEND_CID, 0, r);
	memmove(ctl->cid, r, sizeof ctl->cid);
	io->cmd(SEND_RELATIVE_ADDR, 0, r);
	ctl->rca = r[0]>>16;
	io->cmd(SEND_CSD, ctl->rca<<Rcashift, r);
	memmove(ctl->csd, r, sizeof ctl->csd);
	identify(unit, ctl->csd);
	io->cmd(SELECT_CARD, ctl->rca<<Rcashift, r);
	io->cmd(SET_BLOCKLEN, unit->secsize, r);
	io->cmd(APP_CMD, ctl->rca<<Rcashift, r);
	io->cmd(SET_BUS_WIDTH, Width4, r);
	if(!waserror()){
		mmcswitchfunc(io, Hispeed|Setfunc);
		poperror();
	}
	poperror();
	return 1;
}

static int
mmcrctl(SDunit *unit, char *p, int l)
{
	Ctlr *ctl;
	int i, n;

	ctl = unit->dev->ctlr;
	assert(unit->subno == 0);
	if(unit->sectors == 0){
		mmconline(unit);
		if(unit->sectors == 0)
			return 0;
	}
	n = snprint(p, l, "rca %4.4ux ocr %8.8ux\ncid ", ctl->rca, ctl->ocr);
	for(i = nelem(ctl->cid)-1; i >= 0; i--)
		n += snprint(p+n, l-n, "%8.8ux", ctl->cid[i]);
	n += snprint(p+n, l-n, " csd ");
	for(i = nelem(ctl->csd)-1; i >= 0; i--)
		n += snprint(p+n, l-n, "%8.8ux", ctl->csd[i]);
	n += snprint(p+n, l-n, "\ngeometry %llud %ld %lld 255 63\n",
		unit->sectors, unit->secsize, unit->sectors / (255*63));
	return n;
}

static long
mmcbio(SDunit *unit, int lun, int write, void *data, long nb, uvlong bno)
{
	int len, tries;
	ulong b;
	u32int r[4];
	uchar *buf;
	Ctlr *ctl;
	SDio *io;

	USED(lun);
	ctl = unit->dev->ctlr;
	io = ctl->io;
	assert(unit->subno == 0);
	if(unit->sectors == 0)
		error("media change");
	buf = data;
	len = unit->secsize;
	if(Multiblock){
		b = bno;
		tries = 0;
		while(waserror())
			if(++tries == 3)
				nexterror();
		io->iosetup(write, buf, len, nb);
		if(waserror()){
			io->cmd(STOP_TRANSMISSION, 0, r);
			nexterror();
		}
		io->cmd(write? WRITE_MULTIPLE_BLOCK: READ_MULTIPLE_BLOCK,
			ctl->ocr & Ccs? b: b * len, r);
		io->io(write, buf, nb * len);
		poperror();
		io->cmd(STOP_TRANSMISSION, 0, r);
		poperror();
		b += nb;
	}else{
		for(b = bno; b < bno + nb; b++){
			io->iosetup(write, buf, len, 1);
			io->cmd(write? WRITE_BLOCK : READ_SINGLE_BLOCK,
				ctl->ocr & Ccs? b: b * len, r);
			io->io(write, buf, len);
			buf += len;
		}
	}
	return (b - bno) * len;
}

static int
mmcrio(SDreq*)
{
	return -1;
}

SDifc sdmmcifc = {
	.name	= "mmc",
	.pnp	= mmcpnp,
	.enable	= mmcenable,
	.verify	= mmcverify,
	.online	= mmconline,
	.rctl	= mmcrctl,
	.bio	= mmcbio,
	.rio	= mmcrio,
};
md == GoIdle){
		WR(Hconfig, r[Hconfig] & ~Extbus4);
		sdhostclock(Initfreq);
	}

	if(r[Status] & (Errstatus|Dataflag))
		WR(Status, Errstatus|Dataflag);
	sdhost.done = 0;
	WR(Arg, arg);
	WRsoftfpu.c                                                                                              664       0       0           35 12072576613  11013                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../teg2/softfpu.c"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   spi.c                                                                                                  664       0       0         5257 12717335142  10167                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2835 spi controller
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#define SPIREGS	(VIRTIO+0x204000)
#define	SPI0_CE1_N	7		/* P1 pin 26 */
#define SPI0_CE0_N	8		/* P1 pin 24 */
#define SPI0_MISO	9		/* P1 pin 21 */
#define	SPI0_MOSI	10		/* P1 pin 19 */
#define	SPI0_SCLK	11		/* P1 pin 23 */

typedef struct Ctlr Ctlr;
typedef struct Spiregs Spiregs;

/*
 * Registers for main SPI controller
 */
struct Spiregs {
	u32int	cs;		/* control and status */
	u32int	data;
	u32int	clkdiv;
	u32int	dlen;
	u32int	lossitoh;
	u32int	dmactl;
};

/*
 * Per-controller info
 */
struct Ctlr {
	Spiregs	*regs;
	QLock	lock;
	Lock	reglock;
	Rendez	r;
};

static Ctlr spi;

enum {
	/* cs */
	Lossi32bit	= 1<<25,
	Lossidma	= 1<<24,
	Cspol2		= 1<<23,
	Cspol1		= 1<<22,
	Cspol0		= 1<<21,
	Rxf		= 1<<20,
	Rxr		= 1<<19,
	Txd		= 1<<18,
	Rxd		= 1<<17,
	Done		= 1<<16,
	Lossi		= 1<<13,
	Ren		= 1<<12,
	Adcs		= 1<<11,	/* automatically deassert chip select (dma) */
	Intr		= 1<<10,
	Intd		= 1<<9,
	Dmaen		= 1<<8,
	Ta		= 1<<7,
	Cspol		= 1<<6,
	Rxclear		= 1<<5,
	Txclear		= 1<<4,
	Cpol		= 1<<3,
	Cpha		= 1<<2,
	Csmask		= 3<<0,
	Csshift		= 0,

	/* dmactl */
	Rpanicshift	= 24,
	Rdreqshift	= 16,
	Tpanicshift	= 8,
	Tdreqshift	= 0,
};

static void
spiinit(void)
{
	spi.regs = (Spiregs*)SPIREGS;
	spi.regs->clkdiv = 250;		/* 1 MHz */
	gpiosel(SPI0_MISO, Alt0);
	gpiosel(SPI0_MOSI, Alt0);
	gpiosel(SPI0_SCLK, Alt0);
	gpiosel(SPI0_CE0_N,  Alt0);
	gpiosel(SPI0_CE1_N,  Alt0);
}

void
spimode(int mode)
{
	if(spi.regs == 0)
		spiinit();
	spi.regs->cs = (spi.regs->cs & ~(Cpha | Cpol)) | (mode << 2);
}

/*
 * According the Broadcom docs, the divisor has to
 * be a power of 2, but an errata says that should
 * be multiple of 2 and scope observations confirm
 * that restricting it to a power of 2 is unnecessary.
 */
void
spiclock(uint mhz)
{
	if(spi.regs == 0)
		spiinit();
	if(mhz == 0) {
		spi.regs->clkdiv = 32768;	/* about 8 KHz */
		return;
	}
	spi.regs->clkdiv = 2 * ((125 + (mhz - 1)) / mhz);
}

void
spirw(uint cs, void *buf, int len)
{
	Spiregs *r;

	assert(cs <= 2);
	assert(len < (1<<16));
	qlock(&spi.lock);
	if(waserror()){
		qunlock(&spi.lock);
		nexterror();
	}
	if(spi.regs == 0)
		spiinit();
	r = spi.regs;
	r->dlen = len;
	r->cs = (r->cs & (Cpha | Cpol)) | (cs << Csshift) | Rxclear | Txclear | Dmaen | Adcs | Ta;
	/*
	 * Start write channel before read channel - cache wb before inv
	 */
	dmastart(DmaChanSpiTx, DmaDevSpiTx, DmaM2D,
		buf, &r->data, len);
	dmastart(DmaChanSpiRx, DmaDevSpiRx, DmaD2M,
		&r->data, buf, len);
	if(dmawait(DmaChanSpiRx) < 0)
		error(Eio);
	cachedinvse(buf, len);
	r->cs &= (Cpha | Cpol);
	qunlock(&spi.lock);
	poperror();
}
RITE_BLOCK	= 24,
	WRITE_MULTIPLE_BLOCK= 25,
	APP_CMD		= 55,	/* prefix for following app-specific commands */
	SET_BUS_WIDTH	= 6,
	SD_SEND_OP_COND	= 41,

	/* Command arguments */
	/* SD_SEND_IF_COND */
	Voltage		= 1<<8,
	Checkpattern	= 0x42,

	/* SELECT_CARD */
	Rcashift	= 16,

	/* SD_SEND_OP_COND */
	Hcs	= 1<<30,	/* host supports SDHC syscall.c                                                                                              664       0       0           33 12046513042  10761                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "../kw/syscall.c"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     taslock.c                                                                                              664       0       0        10617 12666255061  11054                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/edf.h"

long maxlockcycles;
long maxilockcycles;
long cumlockcycles;
long cumilockcycles;
ulong maxlockpc;
ulong maxilockpc;

struct
{
	ulong	locks;
	ulong	glare;
	ulong	inglare;
} lockstats;

static void
inccnt(Ref *r)
{
	_xinc(&r->ref);
}

static int
deccnt(Ref *r)
{
	int x;

	x = _xdec(&r->ref);
	if(x < 0)
		panic("deccnt pc=%#p", getcallerpc(&r));
	return x;
}

static void
dumplockmem(char *tag, Lock *l)
{
	uchar *cp;
	int i;

	iprint("%s: ", tag);
	cp = (uchar*)l;
	for(i = 0; i < 64; i++)
		iprint("%2.2ux ", cp[i]);
	iprint("\n");
}

void
lockloop(Lock *l, ulong pc)
{
	Proc *p;

	p = l->p;
	print("lock %#p loop key %#lux pc %#lux held by pc %#lux proc %lud\n",
		l, l->key, pc, l->pc, p ? p->pid : 0);
	dumpaproc(up);
	if(p != nil)
		dumpaproc(p);
}

int
lock(Lock *l)
{
	int i;
	ulong pc;

	pc = getcallerpc(&l);

	lockstats.locks++;
	if(up)
		inccnt(&up->nlocks);	/* prevent being scheded */
	if(tas(&l->key) == 0){
		if(up)
			up->lastlock = l;
		l->pc = pc;
		l->p = up;
		l->isilock = 0;
#ifdef LOCKCYCLES
		l->lockcycles = -lcycles();
#endif
		return 0;
	}
	if(up)
		deccnt(&up->nlocks);

	lockstats.glare++;
	for(;;){
		lockstats.inglare++;
		i = 0;
		while(l->key){
			if(conf.nmach < 2 && up && up->edf && (up->edf->flags & Admitted)){
				/*
				 * Priority inversion, yield on a uniprocessor; on a
				 * multiprocessor, the other processor will unlock
				 */
				print("inversion %#p pc %#lux proc %lud held by pc %#lux proc %lud\n",
					l, pc, up ? up->pid : 0, l->pc, l->p ? l->p->pid : 0);
				up->edf->d = todget(nil);	/* yield to process with lock */
			}
			if(i++ > 100000000){
				i = 0;
				lockloop(l, pc);
			}
		}
		if(up)
			inccnt(&up->nlocks);
		if(tas(&l->key) == 0){
			if(up)
				up->lastlock = l;
			l->pc = pc;
			l->p = up;
			l->isilock = 0;
#ifdef LOCKCYCLES
			l->lockcycles = -lcycles();
#endif
			return 1;
		}
		if(up)
			deccnt(&up->nlocks);
	}
}

void
ilock(Lock *l)
{
	ulong x;
	ulong pc;

	pc = getcallerpc(&l);
	lockstats.locks++;

	x = splhi();
	if(tas(&l->key) != 0){
		lockstats.glare++;
		/*
		 * Cannot also check l->pc, l->m, or l->isilock here
		 * because they might just not be set yet, or
		 * (for pc and m) the lock might have just been unlocked.
		 */
		for(;;){
			lockstats.inglare++;
			splx(x);
			while(l->key)
				;
			x = splhi();
			if(tas(&l->key) == 0)
				goto acquire;
		}
	}
acquire:
	m->ilockdepth++;
	if(up)
		up->lastilock = l;
	l->sr = x;
	l->pc = pc;
	l->p = up;
	l->isilock = 1;
	l->m = MACHP(m->machno);
#ifdef LOCKCYCLES
	l->lockcycles = -lcycles();
#endif
}

int
canlock(Lock *l)
{
	if(up)
		inccnt(&up->nlocks);
	if(tas(&l->key)){
		if(up)
			deccnt(&up->nlocks);
		return 0;
	}

	if(up)
		up->lastlock = l;
	l->pc = getcallerpc(&l);
	l->p = up;
	l->m = MACHP(m->machno);
	l->isilock = 0;
#ifdef LOCKCYCLES
	l->lockcycles = -lcycles();
#endif
	return 1;
}

void
unlock(Lock *l)
{
#ifdef LOCKCYCLES
	l->lockcycles += lcycles();
	cumlockcycles += l->lockcycles;
	if(l->lockcycles > maxlockcycles){
		maxlockcycles = l->lockcycles;
		maxlockpc = l->pc;
	}
#endif
	if(l->key == 0)
		print("unlock: not locked: pc %#p\n", getcallerpc(&l));
	if(l->isilock)
		print("unlock of ilock: pc %lux, held by %lux\n", getcallerpc(&l), l->pc);
	if(l->p != up)
		print("unlock: up changed: pc %#p, acquired at pc %lux, lock p %#p, unlock up %#p\n", getcallerpc(&l), l->pc, l->p, up);
	l->m = nil;
	coherence();
	l->key = 0;
	coherence();

	if(up && deccnt(&up->nlocks) == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

ulong ilockpcs[0x100] = { [0xff] = 1 };
static int n;

void
iunlock(Lock *l)
{
	ulong sr;

#ifdef LOCKCYCLES
	l->lockcycles += lcycles();
	cumilockcycles += l->lockcycles;
	if(l->lockcycles > maxilockcycles){
		maxilockcycles = l->lockcycles;
		maxilockpc = l->pc;
	}
	if(l->lockcycles > 2400)
		ilockpcs[n++ & 0xff]  = l->pc;
#endif
	if(l->key == 0)
		print("iunlock: not locked: pc %#p\n", getcallerpc(&l));
	if(!l->isilock)
		print("iunlock of lock: pc %#p, held by %#lux\n", getcallerpc(&l), l->pc);
	if(islo())
		print("iunlock while lo: pc %#p, held by %#lux\n", getcallerpc(&l), l->pc);

	sr = l->sr;
	l->m = nil;
	coherence();
	l->key = 0;
	coherence();
	m->ilockdepth--;
	if(up)
		up->lastilock = nil;
	splx(sr);
}
                                                                                                                 trap.c                                                                                                 664       0       0        32743 12764026043  10361                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * traps, exceptions, interrupts, system calls.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "arm.h"

#define INTREGS		(VIRTIO+0xB200)
#define	LOCALREGS	(VIRTIO+IOSIZE)

typedef struct Intregs Intregs;
typedef struct Vctl Vctl;

enum {
	Debug = 0,

	Nvec = 8,		/* # of vectors at start of lexception.s */
	Fiqenable = 1<<7,

	Localtimerint	= 0x40,
	Localmboxint	= 0x50,
	Localintpending	= 0x60,
};

/*
 *   Layout at virtual address KZERO (double mapped at HVECTORS).
 */
typedef struct Vpage0 {
	void	(*vectors[Nvec])(void);
	u32int	vtable[Nvec];
} Vpage0;

/*
 * interrupt control registers
 */
struct Intregs {
	u32int	ARMpending;
	u32int	GPUpending[2];
	u32int	FIQctl;
	u32int	GPUenable[2];
	u32int	ARMenable;
	u32int	GPUdisable[2];
	u32int	ARMdisable;
};

struct Vctl {
	Vctl	*next;
	int	irq;
	int	cpu;
	u32int	*reg;
	u32int	mask;
	void	(*f)(Ureg*, void*);
	void	*a;
};

static Lock vctllock;
static Vctl *vctl, *vfiq;

static char *trapnames[PsrMask+1] = {
	[ PsrMusr ] "user mode",
	[ PsrMfiq ] "fiq interrupt",
	[ PsrMirq ] "irq interrupt",
	[ PsrMsvc ] "svc/swi exception",
	[ PsrMabt ] "prefetch abort/data abort",
	[ PsrMabt+1 ] "data abort",
	[ PsrMund ] "undefined instruction",
	[ PsrMsys ] "sys trap",
};

extern int notify(Ureg*);

/*
 *  set up for exceptions
 */
void
trapinit(void)
{
	Vpage0 *vpage0;

	if (m->machno == 0) {
		/* disable everything */
		intrsoff();
		/* set up the exception vectors */
		vpage0 = (Vpage0*)HVECTORS;
		memmove(vpage0->vectors, vectors, sizeof(vpage0->vectors));
		memmove(vpage0->vtable, vtable, sizeof(vpage0->vtable));
		cacheuwbinv();
		l2cacheuwbinv();
	}

	/* set up the stacks for the interrupt modes */
	setr13(PsrMfiq, (u32int*)(FIQSTKTOP));
	setr13(PsrMirq, m->sirq);
	setr13(PsrMabt, m->sabt);
	setr13(PsrMund, m->sund);
	setr13(PsrMsys, m->ssys);

	coherence();
}

void
intrcpushutdown(void)
{
	u32int *enable;

	if(soc.armlocal == 0)
		return;
	enable = (u32int*)(LOCALREGS + Localtimerint) + m->machno;
	*enable = 0;
	if(m->machno){
		enable = (u32int*)(LOCALREGS + Localmboxint) + m->machno;
		*enable = 1;
	}
}

void
intrsoff(void)
{
	Intregs *ip;
	int disable;

	ip = (Intregs*)INTREGS;
	disable = ~0;
	ip->GPUdisable[0] = disable;
	ip->GPUdisable[1] = disable;
	ip->ARMdisable = disable;
	ip->FIQctl = 0;
}

/* called from cpu0 after other cpus are shutdown */
void
intrshutdown(void)
{
	intrsoff();
	intrcpushutdown();
}

static void
intrtime(void)
{
	ulong diff;
	ulong x;

	x = perfticks();
	diff = x - m->perf.intrts;
	m->perf.intrts = 0;

	m->perf.inintr += diff;
	if(up == nil && m->perf.inidle > diff)
		m->perf.inidle -= diff;
}


/*
 *  called by trap to handle irq interrupts.
 *  returns true iff a clock interrupt, thus maybe reschedule.
 */
static int
irq(Ureg* ureg)
{
	Vctl *v;
	int clockintr;
	int found;

	m->perf.intrts = perfticks();
	clockintr = 0;
	found = 0;
	for(v = vctl; v; v = v->next)
		if(v->cpu == m->machno && (*v->reg & v->mask) != 0){
			found = 1;
			coherence();
			v->f(ureg, v->a);
			coherence();
			if(v->irq == IRQclock || v->irq == IRQcntps || v->irq == IRQcntpns)
				clockintr = 1;
		}
	if(!found)
		m->spuriousintr++;
	intrtime();
	return clockintr;
}

/*
 * called direct from lexception.s to handle fiq interrupt.
 */
void
fiq(Ureg *ureg)
{
	Vctl *v;
	int inintr;

	if(m->perf.intrts)
		inintr = 1;
	else{
		inintr = 0;
		m->perf.intrts = perfticks();
	}
	v = vfiq;
	if(v == nil)
		panic("cpu%d: unexpected item in bagging area", m->machno);
	m->intr++;
	ureg->pc -= 4;
	coherence();
	v->f(ureg, v->a);
	coherence();
	if(!inintr)
		intrtime();
}

void
irqenable(int irq, void (*f)(Ureg*, void*), void* a)
{
	Vctl *v;
	Intregs *ip;
	u32int *enable;

	ip = (Intregs*)INTREGS;
	v = (Vctl*)malloc(sizeof(Vctl));
	if(v == nil)
		panic("irqenable: no mem");
	v->irq = irq;
	v->cpu = 0;
	if(irq >= IRQlocal){
		v->reg = (u32int*)(LOCALREGS + Localintpending) + m->machno;
		if(irq >= IRQmbox0)
			enable = (u32int*)(LOCALREGS + Localmboxint) + m->machno;
		else
			enable = (u32int*)(LOCALREGS + Localtimerint) + m->machno;
		v->mask = 1 << (irq - IRQlocal);
		v->cpu = m->machno;
	}else if(irq >= IRQbasic){
		enable = &ip->ARMenable;
		v->reg = &ip->ARMpending;
		v->mask = 1 << (irq - IRQbasic);
	}else{
		enable = &ip->GPUenable[irq/32];
		v->reg = &ip->GPUpending[irq/32];
		v->mask = 1 << (irq % 32);
	}
	v->f = f;
	v->a = a;
	lock(&vctllock);
	if(irq == IRQfiq){
		assert((ip->FIQctl & Fiqenable) == 0);
		assert((*enable & v->mask) == 0);
		vfiq = v;
		ip->FIQctl = Fiqenable | irq;
	}else{
		v->next = vctl;
		vctl = v;
		if(irq >= IRQmbox0){
			if(irq <= IRQmbox3)
				*enable |= 1 << (irq - IRQmbox0);
		}else if(irq >= IRQlocal)
			*enable |= 1 << (irq - IRQlocal);
		else
			*enable = v->mask;
	}
	unlock(&vctllock);
}

static char *
trapname(int psr)
{
	char *s;

	s = trapnames[psr & PsrMask];
	if(s == nil)
		s = "unknown trap number in psr";
	return s;
}

/* this is quite helpful during mmu and cache debugging */
static void
ckfaultstuck(uintptr va)
{
	static int cnt, lastpid;
	static uintptr lastva;

	if (va == lastva && up->pid == lastpid) {
		++cnt;
		if (cnt >= 2)
			/* fault() isn't fixing the underlying cause */
			panic("fault: %d consecutive faults for va %#p",
				cnt+1, va);
	} else {
		cnt = 0;
		lastva = va;
		lastpid = up->pid;
	}
}

/*
 *  called by trap to handle access faults
 */
static void
faultarm(Ureg *ureg, uintptr va, int user, int read)
{
	int n, insyscall;
	char buf[ERRMAX];

	if(up == nil) {
		//dumpregs(ureg);
		panic("fault: nil up in faultarm, pc %#p accessing %#p", ureg->pc, va);
	}
	insyscall = up->insyscall;
	up->insyscall = 1;
	if (Debug)
		ckfaultstuck(va);

	n = fault(va, read);
	if(n < 0){
		if(!user){
			dumpregs(ureg);
			panic("fault: kernel accessing %#p", va);
		}
		/* don't dump registers; programs suicide all the time */
		snprint(buf, sizeof buf, "sys: trap: fault %s va=%#p",
			read? "read": "write", va);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
}

/*
 *  returns 1 if the instruction writes memory, 0 otherwise
 */
int
writetomem(ulong inst)
{
	/* swap always write memory */
	if((inst & 0x0FC00000) == 0x01000000)
		return 1;

	/* loads and stores are distinguished by bit 20 */
	if(inst & (1<<20))
		return 0;

	return 1;
}

/*
 *  here on all exceptions other than syscall (SWI) and fiq
 */
void
trap(Ureg *ureg)
{
	int clockintr, user, x, rv, rem;
	ulong inst, fsr;
	uintptr va;
	char buf[ERRMAX];

	assert(!islo());
	if(up != nil)
		rem = ((char*)ureg)-up->kstack;
	else
		rem = ((char*)ureg)-((char*)m+sizeof(Mach));
	if(rem < 256) {
		iprint("trap: %d stack bytes left, up %#p ureg %#p at pc %#lux\n",
			rem, up, ureg, ureg->pc);
		delay(1000);
		dumpstack();
		panic("trap: %d stack bytes left, up %#p ureg %#p at pc %#lux",
			rem, up, ureg, ureg->pc);
	}

	user = (ureg->psr & PsrMask) == PsrMusr;
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	/*
	 * All interrupts/exceptions should be resumed at ureg->pc-4,
	 * except for Data Abort which resumes at ureg->pc-8.
	 */
	if(ureg->type == (PsrMabt+1))
		ureg->pc -= 8;
	else
		ureg->pc -= 4;

	clockintr = 0;		/* if set, may call sched() before return */
	switch(ureg->type){
	default:
		panic("unknown trap; type %#lux, psr mode %#lux pc %lux", ureg->type,
			ureg->psr & PsrMask, ureg->pc);
		break;
	case PsrMirq:
		clockintr = irq(ureg);
		m->intr++;
		break;
	case PsrMabt:			/* prefetch fault */
		x = ifsrget();
		fsr = (x>>7) & 0x8 | x & 0x7;
		switch(fsr){
		case 0x02:		/* instruction debug event (BKPT) */
			if(user)
				postnote(up, 1, "sys: breakpoint", NDebug);
			else{
				iprint("kernel bkpt: pc %#lux inst %#ux\n",
					ureg->pc, *(u32int*)ureg->pc);
				panic("kernel bkpt");
			}
			break;
		default:
			faultarm(ureg, ureg->pc, user, 1);
			break;
		}
		break;
	case PsrMabt+1:			/* data fault */
		va = farget();
		inst = *(ulong*)(ureg->pc);
		/* bits 12 and 10 have to be concatenated with status */
		x = fsrget();
		fsr = (x>>7) & 0x20 | (x>>6) & 0x10 | x & 0xf;
		switch(fsr){
		default:
		case 0xa:		/* ? was under external abort */
			panic("unknown data fault, 6b fsr %#lux", fsr);
			break;
		case 0x0:
			panic("vector exception at %#lux", ureg->pc);
			break;
		case 0x1:		/* alignment fault */
		case 0x3:		/* access flag fault (section) */
			if(user){
				snprint(buf, sizeof buf,
					"sys: alignment: va %#p", va);
				postnote(up, 1, buf, NDebug);
			} else
				panic("kernel alignment: pc %#lux va %#p", ureg->pc, va);
			break;
		case 0x2:
			panic("terminal exception at %#lux", ureg->pc);
			break;
		case 0x4:		/* icache maint fault */
		case 0x6:		/* access flag fault (page) */
		case 0x8:		/* precise external abort, non-xlat'n */
		case 0x28:
		case 0xc:		/* l1 translation, precise ext. abort */
		case 0x2c:
		case 0xe:		/* l2 translation, precise ext. abort */
		case 0x2e:
		case 0x16:		/* imprecise ext. abort, non-xlt'n */
		case 0x36:
			panic("external abort %#lux pc %#lux addr %#p",
				fsr, ureg->pc, va);
			break;
		case 0x1c:		/* l1 translation, precise parity err */
		case 0x1e:		/* l2 translation, precise parity err */
		case 0x18:		/* imprecise parity or ecc err */
			panic("translation parity error %#lux pc %#lux addr %#p",
				fsr, ureg->pc, va);
			break;
		case 0x5:		/* translation fault, no section entry */
		case 0x7:		/* translation fault, no page entry */
			faultarm(ureg, va, user, !writetomem(inst));
			break;
		case 0x9:
		case 0xb:
			/* domain fault, accessing something we shouldn't */
			if(user){
				snprint(buf, sizeof buf,
					"sys: access violation: va %#p", va);
				postnote(up, 1, buf, NDebug);
			} else
				panic("kernel access violation: pc %#lux va %#p",
					ureg->pc, va);
			break;
		case 0xd:
		case 0xf:
			/* permission error, copy on write or real permission error */
			faultarm(ureg, va, user, !writetomem(inst));
			break;
		}
		break;
	case PsrMund:			/* undefined instruction */
		if(user){
			if(seg(up, ureg->pc, 0) != nil &&
			   *(u32int*)ureg->pc == 0xD1200070)
				postnote(up, 1, "sys: breakpoint", NDebug);
			else{
				/* look for floating point instructions to interpret */
				rv = fpuemu(ureg);
				if(rv == 0)
					postnote(up, 1, "sys: undefined instruction", NDebug);
			}
		}else{
			if (ureg->pc & 3) {
				iprint("rounding fault pc %#lux down to word\n",
					ureg->pc);
				ureg->pc &= ~3;
			}
			iprint("undefined instruction: pc %#lux inst %#ux\n",
				ureg->pc, *(u32int*)ureg->pc);
			panic("undefined instruction");
		}
		break;
	}
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();		/* can cause more traps */
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

int
isvalidaddr(void *v)
{
	return (uintptr)v >= KZERO;
}

static void
dumplongs(char *msg, ulong *v, int n)
{
	int i, l;

	l = 0;
	iprint("%s at %.8p: ", msg, v);
	for(i=0; i<n; i++){
		if(l >= 4){
			iprint("\n    %.8p: ", v);
			l = 0;
		}
		if(isvalidaddr(v)){
			iprint(" %.8lux", *v++);
			l++;
		}else{
			iprint(" invalid");
			break;
		}
	}
	iprint("\n");
}

static void
dumpstackwithureg(Ureg *ureg)
{
	uintptr l, i, v, estack;
	u32int *p;
	char *s;

	if((s = getconf("*nodumpstack")) != nil && strcmp(s, "0") != 0){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("ktrace /kernel/path %#.8lux %#.8lux %#.8lux # pc, sp, link\n",
		ureg->pc, ureg->sp, ureg->r14);
	delay(2000);
	i = 0;
	if(up != nil && (uintptr)&l <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;
	else if((uintptr)&l >= (uintptr)m->stack
	     && (uintptr)&l <= (uintptr)m+MACHSIZE)
		estack = (uintptr)m+MACHSIZE;
	else{
		if(up != nil)
			iprint("&up->kstack %#p &l %#p\n", up->kstack, &l);
		else
			iprint("&m %#p &l %#p\n", m, &l);
		return;
	}
	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if(KTZERO < v && v < (uintptr)etext && !(v & 3)){
			v -= sizeof(u32int);		/* back up an instr */
			p = (u32int*)v;
			if((*p & 0x0f000000) == 0x0b000000){	/* BL instr? */
				iprint("%#8.8lux=%#8.8lux ", l, v);
				i++;
			}
		}
		if(i == 4){
			i = 0;
			iprint("\n");
		}
	}
	if(i)
		iprint("\n");
}

/*
 * Fill in enough of Ureg to get a stack trace, and call a function.
 * Used by debugging interface rdb.
 */

static void
getpcsp(ulong *pc, ulong *sp)
{
	*pc = getcallerpc(&pc);
	*sp = (ulong)&pc-4;
}

void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;

	getpcsp((ulong*)&ureg.pc, (ulong*)&ureg.sp);
	ureg.r14 = getcallerpc(&fn);
	fn(&ureg);
}

void
dumpstack(void)
{
	callwithureg(dumpstackwithureg);
}

void
dumpregs(Ureg* ureg)
{
	int s;

	if (ureg == nil) {
		iprint("trap: no user process\n");
		return;
	}
	s = splhi();
	iprint("trap: %s", trapname(ureg->type));
	if(ureg != nil && (ureg->psr & PsrMask) != PsrMsvc)
		iprint(" in %s", trapname(ureg->psr));
	iprint("\n");
	iprint("psr %8.8lux type %2.2lux pc %8.8lux link %8.8lux\n",
		ureg->psr, ureg->type, ureg->pc, ureg->link);
	iprint("R14 %8.8lux R13 %8.8lux R12 %8.8lux R11 %8.8lux R10 %8.8lux\n",
		ureg->r14, ureg->r13, ureg->r12, ureg->r11, ureg->r10);
	iprint("R9  %8.8lux R8  %8.8lux R7  %8.8lux R6  %8.8lux R5  %8.8lux\n",
		ureg->r9, ureg->r8, ureg->r7, ureg->r6, ureg->r5);
	iprint("R4  %8.8lux R3  %8.8lux R2  %8.8lux R1  %8.8lux R0  %8.8lux\n",
		ureg->r4, ureg->r3, ureg->r2, ureg->r1, ureg->r0);
	iprint("stack is at %#p\n", ureg);
	iprint("pc %#lux link %#lux\n", ureg->pc, ureg->link);

	if(up)
		iprint("user stack: %#p-%#p\n", up->kstack, up->kstack+KSTACK-4);
	else
		iprint("kernel stack: %8.8lux-%8.8lux\n",
			(ulong)(m+1), (ulong)m+BY2PG-4);
	dumplongs("stack", (ulong *)(ureg + 1), 16);
	delay(2000);
	dumpstack();
	splx(s);
}
gging area", m->machno);
	m->trap4.c                                                                                                664       0       0        53015 13526727726  10455                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * Adapted from ../teg2/trap.c
 *
 * arm mpcore generic interrupt controller (gic) v1
 * traps, exceptions, interrupts, system calls.
 *
 * there are two pieces: the interrupt distributor and the cpu interface.
 *
 * memset or memmove on any of the distributor registers generates an
 * exception like this one:
 *	panic: external abort 0x28 pc 0xc048bf68 addr 0x50041800
 *
 * we use l1 and l2 cache ops to force vectors to be visible everywhere.
 *
 * apparently irqs 0—15 (SGIs) are always enabled.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "ureg.h"
#include "arm.h"

#define IRQLOCAL(irq)	((irq) - IRQlocal + 13 + 16)
#define IRQGLOBAL(irq)	((irq) + 64 + 32)

#define ISSGI(irq)	((uint)(irq) < Nsgi)

enum {
	Debug = 0,

	Intrdist = 0x41000,
	Intrcpu = 0x42000,

	Nvec = 8,		/* # of vectors at start of lexception.s */
	Bi2long = BI2BY * sizeof(long),
	Nirqs = 1024,
	Nsgi =	16,		/* software-generated (inter-processor) intrs */
	Nppi =	32,		/* sgis + other private peripheral intrs */
};

typedef struct Intrcpuregs Intrcpuregs;
typedef struct Intrdistregs Intrdistregs;

/*
 * almost this entire register set is buggered.
 * the distributor is supposed to be per-system, not per-cpu,
 * yet some registers are banked per-cpu, as marked.
 */
struct Intrdistregs {			/* distributor */
	ulong	ctl;
	ulong	ctlrtype;
	ulong	distid;
	uchar	_pad0[0x80 - 0xc];

	/* botch: *[0] are banked per-cpu from here */
	/* bit maps */
	ulong	grp[32];		/* in group 1 (non-secure) */
	ulong	setena[32];		/* forward to cpu interfaces */
	ulong	clrena[32];
	ulong	setpend[32];
	ulong	clrpend[32];
	ulong	setact[32];		/* active? */
	ulong	clract[32];
	/* botch: *[0] are banked per-cpu until here */

	uchar	pri[1020];	/* botch: pri[0] — pri[7] are banked per-cpu */
	ulong	_rsrvd1;
	/* botch: targ[0] through targ[7] are banked per-cpu and RO */
	uchar	targ[1020];	/* byte bit maps: cpu targets indexed by intr */
	ulong	_rsrvd2;
	/* botch: cfg[1] is banked per-cpu */
	ulong	cfg[64];		/* bit pairs: edge? 1-N? */
	ulong	_pad1[64];
	ulong	nsac[64];		/* bit pairs (v2 only) */

	/* software-generated intrs (a.k.a. sgi) */
	ulong	swgen;			/* intr targets */
	uchar	_pad2[0xf10 - 0xf04];
	uchar	clrsgipend[16];		/* bit map (v2 only) */
	uchar	setsgipend[16];		/* bit map (v2 only) */
};

enum {
	/* ctl bits */
	Forw2cpuif =	1,

	/* ctlrtype bits */
	Cpunoshft =	5,
	Cpunomask =	MASK(3),
	Intrlines =	MASK(5),

	/* cfg bits */
	Level =		0<<1,
	Edge =		1<<1,		/* edge-, not level-sensitive */
	Toall =		0<<0,
	To1 =		1<<0,		/* vs. to all */

	/* swgen bits */
	Totargets =	0,
	Tonotme =	1<<24,
	Tome =		2<<24,
};

/* each cpu sees its own registers at the same base address ((ARMLOCAL+Intrcpu)) */
struct Intrcpuregs {
	ulong	ctl;
	ulong	primask;

	ulong	binpt;			/* group pri vs subpri split */
	ulong	ack;
	ulong	end;
	ulong	runpri;
	ulong	hipripend;

	/* aliased regs (secure, for group 1) */
	ulong	alibinpt;
	ulong	aliack;			/* (v2 only) */
	ulong	aliend;			/* (v2 only) */
	ulong	alihipripend;		/* (v2 only) */

	uchar	_pad0[0xd0 - 0x2c];
	ulong	actpri[4];		/* (v2 only) */
	ulong	nsactpri[4];		/* (v2 only) */

	uchar	_pad0[0xfc - 0xf0];
	ulong	ifid;			/* ro */

	uchar	_pad0[0x1000 - 0x100];
	ulong	deact;			/* wo (v2 only) */
};

enum {
	/* ctl bits */
	Enable =	1,
	Eoinodeact =	1<<9,		/* (v2 only) */

	/* (ali) ack/end/hipriend/deact bits */
	Intrmask =	MASK(10),
	Cpuidshift =	10,
	Cpuidmask =	MASK(3),

	/* ifid bits */
	Archversshift =	16,
	Archversmask =	MASK(4),
};

typedef struct Vctl Vctl;
typedef struct Vctl {
	Vctl*	next;		/* handlers on this vector */
	char	*name;		/* of driver, xallocated */
	void	(*f)(Ureg*, void*);	/* handler to call */
	void*	a;		/* argument to call it with */
} Vctl;

static Lock vctllock;
static Vctl* vctl[Nirqs];

/*
 *   Layout at virtual address 0.
 */
typedef struct Vpage0 {
	void	(*vectors[Nvec])(void);
	u32int	vtable[Nvec];
} Vpage0;

enum
{
	Ntimevec = 20		/* number of time buckets for each intr */
};
ulong intrtimes[Nirqs][Ntimevec];

int irqtooearly = 0;

static ulong shadena[32];	/* copy of enable bits, saved by intcmaskall */
static Lock distlock;

extern int notify(Ureg*);

static void dumpstackwithureg(Ureg *ureg);

void
printrs(int base, ulong word)
{
	int bit;

	for (bit = 0; word; bit++, word >>= 1)
		if (word & 1)
			iprint(" %d", base + bit);
}

void
dumpintrs(char *what, ulong *bits)
{
	int i, first, some;
	ulong word;
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	first = 1;
	some = 0;
	USED(idp);
	for (i = 0; i < nelem(idp->setpend); i++) {
		word = bits[i];
		if (word) {
			if (first) {
				first = 0;
				iprint("%s", what);
			}
			some = 1;
			printrs(i * Bi2long, word);
		}
	}
	if (!some)
		iprint("%s none", what);
	iprint("\n");
}

void
dumpintrpend(void)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	iprint("\ncpu%d gic regs:\n", m->machno);
	dumpintrs("group 1", idp->grp);
	dumpintrs("enabled", idp->setena);
	dumpintrs("pending", idp->setpend);
	dumpintrs("active ", idp->setact);
}

/*
 *  keep histogram of interrupt service times
 */
void
intrtime(Mach*, int vno)
{
	ulong diff;
	ulong x;

	x = perfticks();
	diff = x - m->perf.intrts;
	m->perf.intrts = x;

	m->perf.inintr += diff;
	if(up == nil && m->perf.inidle > diff)
		m->perf.inidle -= diff;

	if (m->cpumhz == 0)
		return;			/* don't divide by zero */
	diff /= m->cpumhz*100;		/* quantum = 100µsec */
	if(diff >= Ntimevec)
		diff = Ntimevec-1;
	if ((uint)vno >= Nirqs)
		vno = Nirqs-1;
	intrtimes[vno][diff]++;
}

static ulong
intack(Intrcpuregs *icp)
{
	return icp->ack & Intrmask;
}

static void
intdismiss(Intrcpuregs *icp, ulong ack)
{
	icp->end = ack;
	coherence();
}

static int
irqinuse(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	return idp->setena[irq / Bi2long] & (1 << (irq % Bi2long));
}

void
intcunmask(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->setena[irq / Bi2long] = 1 << (irq % Bi2long);
	iunlock(&distlock);
}

void
intcmask(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->clrena[irq / Bi2long] = 1 << (irq % Bi2long);
	iunlock(&distlock);
}

static void
intcmaskall(Intrdistregs *idp)		/* mask all intrs for all cpus */
{
	int i;

	for (i = 0; i < nelem(idp->setena); i++)
		shadena[i] = idp->setena[i];
	for (i = 0; i < nelem(idp->clrena); i++)
		idp->clrena[i] = ~0;
	coherence();
}

static void
intcunmaskall(Intrdistregs *idp)	/* unused */
{
	int i;

	for (i = 0; i < nelem(idp->setena); i++)
		idp->setena[i] = shadena[i];
	coherence();
}

static ulong
permintrs(Intrdistregs *idp, int base, int r)
{
	ulong perms;

	idp->clrena[r] = ~0;		/* disable all */
	coherence();
	perms = idp->clrena[r];
	if (perms) {
		iprint("perm intrs:");
		printrs(base, perms);
		iprint("\n");
	}
	return perms;
}

static void
intrcfg(Intrdistregs *idp)
{
	int i, cpumask;
	ulong pat;

	/* set up all interrupts as level-sensitive, to one cpu (0) */
	pat = 0;
	for (i = 0; i < Bi2long; i += 2)
		pat |= (Level | To1) << i;

	if (m->machno == 0) {			/* system-wide & cpu0 cfg */
		for (i = 0; i < nelem(idp->grp); i++)
			idp->grp[i] = 0;		/* secure */
		for (i = 0; i < nelem(idp->pri); i++)
			idp->pri[i] = 0;		/* highest priority */
		/* set up all interrupts as level-sensitive, to one cpu (0) */
		for (i = 0; i < nelem(idp->cfg); i++)
			idp->cfg[i] = pat;
		/* first Nppi are read-only for SGIs and PPIs */
		cpumask = 1<<0;				/* just cpu 0 */
		for (i = Nppi; i < sizeof idp->targ; i++)
			idp->targ[i] = cpumask;
		coherence();

		intcmaskall(idp);
		for (i = 0; i < nelem(idp->clrena); i++) {
			// permintrs(idp, i * Bi2long, i);
			idp->clrpend[i] = idp->clract[i] = idp->clrena[i] = ~0;
		}
	} else {				/* per-cpu config */
		idp->grp[0] = 0;		/* secure */
		for (i = 0; i < 8; i++)
			idp->pri[i] = 0;	/* highest priority */
		/* idp->targ[0 through Nppi-1] are supposed to be read-only */
		for (i = 0; i < Nppi; i++)
			idp->targ[i] = 1<<m->machno;
		idp->cfg[1] = pat;
		coherence();

		// permintrs(idp, i * Bi2long, i);
		idp->clrpend[0] = idp->clract[0] = idp->clrena[0] = ~0;
		/* on cpu1, irq Extpmuirq (118) is always pending here */
	}
	coherence();
}

void
intrto(int cpu, int irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	/* first Nppi are read-only for SGIs and the like */
	ilock(&distlock);
	idp->targ[irq] = 1 << cpu;
	iunlock(&distlock);
}

void
intrsto(int cpu)			/* unused */
{
	int i;
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	/* first Nppi are read-only for SGIs and the like */
	for (i = Nppi; i < sizeof idp->targ; i++)
		intrto(cpu, i);
	USED(idp);
}

void
intrcpu(int cpu)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->swgen = Totargets | 1 << (cpu + 16) | m->machno;
	iunlock(&distlock);
}

/*
 *  set up for exceptions
 */
void
trapinit(void)
{
	int s;
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);
	Intrcpuregs *icp = (Intrcpuregs *)(ARMLOCAL+Intrcpu);
	Vpage0 *vpage0;
	enum { Vecsize = sizeof vpage0->vectors + sizeof vpage0->vtable, };

	/*
	 * set up the exception vectors, high and low.
	 *
	 * we can't use cache ops on HVECTORS address, since they
	 * work on virtual addresses, and only those that have a
	 * physical address == PADDR(virtual).
	 */
	if (m->machno == 0) {
		vpage0 = (Vpage0*)HVECTORS;
		memmove(vpage0->vectors, vectors, sizeof(vpage0->vectors));
		memmove(vpage0->vtable, vtable, sizeof(vpage0->vtable));

		vpage0 = (Vpage0*)KADDR(0);
		memmove(vpage0->vectors, vectors, sizeof(vpage0->vectors));
		memmove(vpage0->vtable, vtable, sizeof(vpage0->vtable));

		cacheuwbinv();
		l2cacheuwbinv();
	}

	/*
	 * set up the stack pointers for the exception modes for this cpu.
	 * they point to small `save areas' in Mach, not actual stacks.
	 */
	s = splhi();			/* make these modes ignore intrs too */
	setr13(PsrMfiq, (u32int*)(FIQSTKTOP));
	setr13(PsrMirq, m->sirq);
	setr13(PsrMabt, m->sabt);
	setr13(PsrMund, m->sund);
	setr13(PsrMsys, m->ssys);
	splx(s);

	assert((idp->distid & MASK(12)) == 0x43b);	/* made by arm */
	assert((icp->ifid   & MASK(12)) == 0x43b);	/* made by arm */

	ilock(&distlock);
	idp->ctl = 0;
	icp->ctl = 0;
	coherence();

	intrcfg(idp);			/* some per-cpu cfg here */

	icp->ctl = Enable;
	icp->primask = (uchar)~0;	/* let all priorities through */
	coherence();

	idp->ctl = Forw2cpuif;
	iunlock(&distlock);
}

void
intrsoff(void)
{
	ilock(&distlock);
	intcmaskall((Intrdistregs *)(ARMLOCAL+Intrdist));
	iunlock(&distlock);
}

void
intrcpushutdown(void)
{
	Intrcpuregs *icp = (Intrcpuregs *)(ARMLOCAL+Intrcpu);

	icp->ctl = 0;
	icp->primask = 0;	/* let no priorities through */
	coherence();
}

/* called from cpu0 after other cpus are shutdown */
void
intrshutdown(void)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	intrsoff();
	idp->ctl = 0;
	intrcpushutdown();
}

/*
 *  enable an irq interrupt
 *  note that the same private interrupt may be enabled on multiple cpus
 */
void
irqenable(int irq, void (*f)(Ureg*, void*), void* a)
{
	Vctl *v;
	static char name[] = "anon";

	/* permute irq numbers for pi4 */
	if(irq >= IRQlocal)
		irq = IRQLOCAL(irq);
	else
		irq = IRQGLOBAL(irq);
	if(irq >= nelem(vctl))
		panic("irqenable irq %d", irq);

	if (irqtooearly) {
		iprint("irqenable for %d %s called too early\n", irq, name);
		return;
	}
	/*
	 * if in use, could be a private interrupt on a secondary cpu,
	 * so don't add anything to the vector chain.  irqs should
	 * otherwise be one-to-one with devices.
	 */
	if(!ISSGI(irq) && irqinuse(irq)) {
		lock(&vctllock);
		if (vctl[irq] == nil) {
			dumpintrpend();
			panic("non-sgi irq %d in use yet no Vctl allocated", irq);
		}
		unlock(&vctllock);
	}
	/* could be 1st use of this irq or could be an sgi (always in use) */
	else if (vctl[irq] == nil) {
		v = malloc(sizeof(Vctl));
		if (v == nil)
			panic("irqenable: malloc Vctl");
		v->f = f;
		v->a = a;
		v->name = malloc(strlen(name)+1);
		if (v->name == nil)
			panic("irqenable: malloc name");
		strcpy(v->name, name);

		lock(&vctllock);
		if (vctl[irq] != nil) {
			/* allocation race: someone else did it first */
			free(v->name);
			free(v);
		} else {
			v->next = vctl[irq];
			vctl[irq] = v;
		}
		unlock(&vctllock);
	}
	intcunmask(irq);
}

/*
 *  called by trap to handle access faults
 */
static void
faultarm(Ureg *ureg, uintptr va, int user, int read)
{
	int n, insyscall;

	if(up == nil) {
		dumpstackwithureg(ureg);
		panic("faultarm: cpu%d: nil up, %sing %#p at %#p",
			m->machno, (read? "read": "writ"), va, ureg->pc);
	}
	insyscall = up->insyscall;
	up->insyscall = 1;

	n = fault(va, read);		/* goes spllo */
	splhi();
	if(n < 0){
		char buf[ERRMAX];

		if(!user){
			dumpstackwithureg(ureg);
			panic("fault: cpu%d: kernel %sing %#p at %#p",
				m->machno, read? "read": "writ", va, ureg->pc);
		}
		/* don't dump registers; programs suicide all the time */
		snprint(buf, sizeof buf, "sys: trap: fault %s va=%#p",
			read? "read": "write", va);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
}

/*
 *  called by trap to handle interrupts.
 *  returns true iff a clock interrupt, thus maybe reschedule.
 */
static int
irq(Ureg* ureg)
{
	int clockintr, ack;
	uint irqno, handled;
	Intrcpuregs *icp = (Intrcpuregs *)(ARMLOCAL+Intrcpu);
	Vctl *v;

	clockintr = 0;
again:
	ack = intack(icp);
	irqno = ack & Intrmask;

	if(irqno == 1023)
		return clockintr;
	if(irqno == IRQGLOBAL(IRQclock) || irqno == IRQLOCAL(IRQcntpns))
		clockintr = 1;

	handled = 0;
	for(v = vctl[irqno]; v != nil; v = v->next)
		if (v->f) {
			if (islo())
				panic("trap: pl0 before trap handler for %s",
					v->name);
			coherence();
			v->f(ureg, v->a);
			coherence();
			if (islo())
				panic("trap: %s lowered pl", v->name);
//			splhi();		/* in case v->f lowered pl */
			handled++;
		}
	if(!handled){
		if (irqno >= 1022){
			iprint("cpu%d: ignoring spurious interrupt\n", m->machno);
			return clockintr;
		}else {
			intcmask(irqno);
			iprint("cpu%d: unexpected interrupt %d, now masked\n",
				m->machno, irqno);
		}
	}

	intdismiss(icp, ack);
	intrtime(m, irqno);
	goto again;
}

/*
 *  returns 1 if the instruction writes memory, 0 otherwise
 */
int
writetomem(ulong inst)
{
	/* swap always write memory */
	if((inst & 0x0FC00000) == 0x01000000)
		return 1;

	/* loads and stores are distinguished by bit 20 */
	if(inst & (1<<20))
		return 0;

	return 1;
}

static void
datafault(Ureg *ureg, int user)
{
	int x;
	ulong inst, fsr;
	uintptr va;

	va = farget();

	inst = *(ulong*)(ureg->pc);
	/* bits 12 and 10 have to be concatenated with status */
	x = fsrget();
	fsr = (x>>7) & 0x20 | (x>>6) & 0x10 | x & 0xf;
	switch(fsr){
	default:
	case 0xa:		/* ? was under external abort */
		panic("unknown data fault, 6b fsr %#lux", fsr);
		break;
	case 0x0:
		panic("vector exception at %#lux", ureg->pc);
		break;
	case 0x1:		/* alignment fault */
	case 0x3:		/* access flag fault (section) */
		if(user){
			char buf[ERRMAX];

			snprint(buf, sizeof buf,
				"sys: alignment: pc %#lux va %#p\n",
				ureg->pc, va);
			postnote(up, 1, buf, NDebug);
		} else {
			dumpstackwithureg(ureg);
			panic("kernel alignment: pc %#lux va %#p", ureg->pc, va);
		}
		break;
	case 0x2:
		panic("terminal exception at %#lux", ureg->pc);
		break;
	case 0x4:		/* icache maint fault */
	case 0x6:		/* access flag fault (page) */
	case 0x8:		/* precise external abort, non-xlat'n */
	case 0x28:
	case 0x16:		/* imprecise ext. abort, non-xlt'n */
	case 0x36:
		panic("external non-translation abort %#lux pc %#lux addr %#p",
			fsr, ureg->pc, va);
		break;
	case 0xc:		/* l1 translation, precise ext. abort */
	case 0x2c:
	case 0xe:		/* l2 translation, precise ext. abort */
	case 0x2e:
		panic("external translation abort %#lux pc %#lux addr %#p",
			fsr, ureg->pc, va);
		break;
	case 0x1c:		/* l1 translation, precise parity err */
	case 0x1e:		/* l2 translation, precise parity err */
	case 0x18:		/* imprecise parity or ecc err */
		panic("translation parity error %#lux pc %#lux addr %#p",
			fsr, ureg->pc, va);
		break;
	case 0x5:		/* translation fault, no section entry */
	case 0x7:		/* translation fault, no page entry */
		faultarm(ureg, va, user, !writetomem(inst));
		break;
	case 0x9:
	case 0xb:
		/* domain fault, accessing something we shouldn't */
		if(user){
			char buf[ERRMAX];

			snprint(buf, sizeof buf,
				"sys: access violation: pc %#lux va %#p\n",
				ureg->pc, va);
			postnote(up, 1, buf, NDebug);
		} else
			panic("kernel access violation: pc %#lux va %#p",
				ureg->pc, va);
		break;
	case 0xd:
	case 0xf:
		/* permission error, copy on write or real permission error */
		faultarm(ureg, va, user, !writetomem(inst));
		break;
	}
}

/*
 *  here on all exceptions other than syscall (SWI) and reset
 */
void
trap(Ureg *ureg)
{
	int clockintr, user, rem;
	uintptr va, ifsr;

	splhi();			/* paranoia */
	if(up != nil)
		rem = ((char*)ureg)-up->kstack;
	else
		rem = ((char*)ureg)-((char*)m+sizeof(Mach));
	if(rem < 1024) {
		iprint("trap: %d stack bytes left, up %#p ureg %#p m %#p cpu%d at pc %#lux\n",
			rem, up, ureg, m, m->machno, ureg->pc);
		dumpstackwithureg(ureg);
		panic("trap: %d stack bytes left, up %#p ureg %#p at pc %#lux",
			rem, up, ureg, ureg->pc);
	}

	m->perf.intrts = perfticks();
	user = (ureg->psr & PsrMask) == PsrMusr;
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	/*
	 * All interrupts/exceptions should be resumed at ureg->pc-4,
	 * except for Data Abort which resumes at ureg->pc-8.
	 */
	if(ureg->type == (PsrMabt+1))
		ureg->pc -= 8;
	else
		ureg->pc -= 4;

	clockintr = 0;		/* if set, may call sched() before return */
	switch(ureg->type){
	default:
		panic("unknown trap; type %#lux, psr mode %#lux", ureg->type,
			ureg->psr & PsrMask);
		break;
	case PsrMirq:
		m->intr++;
		clockintr = irq(ureg);
		if(0 && up && !clockintr)
			preempted();	/* this causes spurious suicides */
		break;
	case PsrMabt:			/* prefetch (instruction) fault */
		va = ureg->pc;
		ifsr = ifsrget();
		ifsr = (ifsr>>7) & 0x8 | ifsr & 0x7;
		switch(ifsr){
		case 0x02:		/* instruction debug event (BKPT) */
			if(user)
				postnote(up, 1, "sys: breakpoint", NDebug);
			else{
				iprint("kernel bkpt: pc %#lux inst %#ux\n",
					va, *(u32int*)va);
				panic("kernel bkpt");
			}
			break;
		default:
			faultarm(ureg, va, user, 1);
			break;
		}
		break;
	case PsrMabt+1:			/* data fault */
		datafault(ureg, user);
		break;
	case PsrMund:			/* undefined instruction */
		if(!user) {
			if (ureg->pc & 3) {
				iprint("rounding fault pc %#lux down to word\n",
					ureg->pc);
				ureg->pc &= ~3;
			}
			if (Debug)
				iprint("mathemu: cpu%d fpon %d instr %#8.8lux at %#p\n",
					m->machno, m->fpon, *(ulong *)ureg->pc,
				ureg->pc);
			dumpstackwithureg(ureg);
			panic("cpu%d: undefined instruction: pc %#lux inst %#ux",
				m->machno, ureg->pc, ((u32int*)ureg->pc)[0]);
		} else if(seg(up, ureg->pc, 0) != nil &&
		   *(u32int*)ureg->pc == 0xD1200070)
			postnote(up, 1, "sys: breakpoint", NDebug);
		else if(fpuemu(ureg) == 0){	/* didn't find any FP instrs? */
			char buf[ERRMAX];

			snprint(buf, sizeof buf,
				"undefined instruction: pc %#lux instr %#8.8lux\n",
				ureg->pc, *(ulong *)ureg->pc);
			postnote(up, 1, buf, NDebug);
		}
		break;
	}
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();		/* can cause more traps */
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

/*
 * Fill in enough of Ureg to get a stack trace, and call a function.
 * Used by debugging interface rdb.
 */
void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;

	memset(&ureg, 0, sizeof ureg);
	ureg.pc = getcallerpc(&fn);
	ureg.sp = PTR2UINT(&fn);
	fn(&ureg);
}

static void
dumpstackwithureg(Ureg *ureg)
{
	int x;
	uintptr l, v, i, estack;
	char *s;

	dumpregs(ureg);
	if((s = getconf("*nodumpstack")) != nil && strcmp(s, "0") != 0){
		iprint("dumpstack disabled\n");
		return;
	}
	delay(1000);
	iprint("dumpstack\n");

	x = 0;
	x += iprint("ktrace /kernel/path %#.8lux %#.8lux %#.8lux # pc, sp, link\n",
		ureg->pc, ureg->sp, ureg->r14);
	delay(20);
	i = 0;
	if(up
	&& (uintptr)&l >= (uintptr)up->kstack
	&& (uintptr)&l <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;
	else if((uintptr)&l >= (uintptr)m->stack
	&& (uintptr)&l <= (uintptr)m+MACHSIZE)
		estack = (uintptr)m+MACHSIZE;
	else
		return;
	x += iprint("estackx %p\n", estack);

	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)etext) || estack-l < 32){
			x += iprint("%.8p ", v);
			delay(20);
			i++;
		}
		if(i == 8){
			i = 0;
			x += iprint("\n");
			delay(20);
		}
	}
	if(i)
		iprint("\n");
	delay(3000);
}

void
dumpstack(void)
{
	callwithureg(dumpstackwithureg);
}

/*
 * dump general registers
 */
static void
dumpgpr(Ureg* ureg)
{
	if(up != nil)
		iprint("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);

	delay(20);
	iprint("%#8.8lux\tr0\n", ureg->r0);
	iprint("%#8.8lux\tr1\n", ureg->r1);
	iprint("%#8.8lux\tr2\n", ureg->r2);
	delay(20);
	iprint("%#8.8lux\tr3\n", ureg->r3);
	iprint("%#8.8lux\tr4\n", ureg->r4);
	iprint("%#8.8lux\tr5\n", ureg->r5);
	delay(20);
	iprint("%#8.8lux\tr6\n", ureg->r6);
	iprint("%#8.8lux\tr7\n", ureg->r7);
	iprint("%#8.8lux\tr8\n", ureg->r8);
	delay(20);
	iprint("%#8.8lux\tr9 (up)\n", ureg->r9);
	iprint("%#8.8lux\tr10 (m)\n", ureg->r10);
	iprint("%#8.8lux\tr11 (loader temporary)\n", ureg->r11);
	iprint("%#8.8lux\tr12 (SB)\n", ureg->r12);
	delay(20);
	iprint("%#8.8lux\tr13 (sp)\n", ureg->r13);
	iprint("%#8.8lux\tr14 (link)\n", ureg->r14);
	iprint("%#8.8lux\tr15 (pc)\n", ureg->pc);
	delay(20);
	iprint("%10.10lud\ttype\n", ureg->type);
	iprint("%#8.8lux\tpsr\n", ureg->psr);
	delay(500);
}

void
dumpregs(Ureg* ureg)
{
	dumpgpr(ureg);
}

void
fiq(Ureg *)
{
}
d", irq);
		}
		unlock(&vctllock);
	}
	/* could be 1st use of this irq or could be an sgi (always in use) */
	else if (vctl[irq] == nil) {
		v = malloc(sizeof(Vctl));
		if (v == nil)
			panic("irqenable: malloc Vctl");
		v->f = f;
		v->a = a;
		v->name = malloc(strlen(name)+1);
		if (v->name == nil)
			panic("irqenable: malloc name");
		strcpy(v->name, name);

		lock(&vctllock);
		if (vctl[irq] != nil) {
			/* allocation race: someone else did it first */
			free(v->name);
			free(v);
		} else uartmini.c                                                                                             664       0       0        12475 12752322362  11243                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * bcm2835 mini uart (UART1)
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define AUXREGS		(VIRTIO+0x215000)
#define	OkLed		16
#define	TxPin		14
#define	RxPin		15

/* AUX regs */
enum {
	Irq	= 0x00>>2,
		UartIrq	= 1<<0,
	Enables	= 0x04>>2,
		UartEn	= 1<<0,
	MuIo	= 0x40>>2,
	MuIer	= 0x44>>2,
		RxIen	= 1<<0,
		TxIen	= 1<<1,
	MuIir	= 0x48>>2,
	MuLcr	= 0x4c>>2,
		Bitsmask= 3<<0,
		Bits7	= 2<<0,
		Bits8	= 3<<0,
	MuMcr	= 0x50>>2,
		RtsN	= 1<<1,
	MuLsr	= 0x54>>2,
		TxDone	= 1<<6,
		TxRdy	= 1<<5,
		RxRdy	= 1<<0,
	MuCntl	= 0x60>>2,
		CtsFlow	= 1<<3,
		TxEn	= 1<<1,
		RxEn	= 1<<0,
	MuBaud	= 0x68>>2,
};

extern PhysUart miniphysuart;

static Uart miniuart = {
	.regs	= (u32int*)AUXREGS,
	.name	= "uart0",
	.freq	= 250000000,
	.baud	= 115200,
	.phys	= &miniphysuart,
};

static int baud(Uart*, int);

static void
interrupt(Ureg*, void *arg)
{
	Uart *uart;
	u32int *ap;

	uart = arg;
	ap = (u32int*)uart->regs;

	coherence();
	if(0 && (ap[Irq] & UartIrq) == 0)
		return;
	if(ap[MuLsr] & TxRdy)
		uartkick(uart);
	if(ap[MuLsr] & RxRdy){
		if(uart->console){
			if(uart->opens == 1)
				uart->putc = kbdcr2nl;
			else
				uart->putc = nil;
		}
		do{
			uartrecv(uart, ap[MuIo] & 0xFF);
		}while(ap[MuLsr] & RxRdy);
	}
	coherence();
}

static Uart*
pnp(void)
{
	Uart *uart;

	uart = &miniuart;
	if(uart->console == 0)
		kbdq = qopen(8*1024, 0, nil, nil);
	return uart;
}

static void
enable(Uart *uart, int ie)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	delay(10);
	gpiosel(TxPin, Alt5);
	gpiosel(RxPin, Alt5);
	gpiopulloff(TxPin);
	gpiopullup(RxPin);
	ap[Enables] |= UartEn;
	ap[MuIir] = 6;
	ap[MuLcr] = Bits8;
	ap[MuCntl] = TxEn|RxEn;
	baud(uart, uart->baud);
	if(ie){
		intrenable(IRQaux, interrupt, uart, 0, "uart");
		ap[MuIer] = RxIen|TxIen;
	}else
		ap[MuIer] = 0;
}

static void
disable(Uart *uart)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	ap[MuCntl] = 0;
	ap[MuIer] = 0;
}

static void
kick(Uart *uart)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(uart->blocked)
		return;
	coherence();
	while(ap[MuLsr] & TxRdy){
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		ap[MuIo] = *(uart->op++);
	}
	if(ap[MuLsr] & TxDone)
		ap[MuIer] &= ~TxIen;
	else
		ap[MuIer] |= TxIen;
	coherence();
}

/* TODO */
static void
dobreak(Uart *uart, int ms)
{
	USED(uart, ms);
}

static int
baud(Uart *uart, int n)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(uart->freq == 0 || n <= 0)
		return -1;
	ap[MuBaud] = (uart->freq + 4*n - 1) / (8 * n) - 1;
	uart->baud = n;
	return 0;
}

static int
bits(Uart *uart, int n)
{
	u32int *ap;
	int set;

	ap = (u32int*)uart->regs;
	switch(n){
	case 7:
		set = Bits7;
		break;
	case 8:
		set = Bits8;
		break;
	default:
		return -1;
	}
	ap[MuLcr] = (ap[MuLcr] & ~Bitsmask) | set;
	uart->bits = n;
	return 0;
}

static int
stop(Uart *uart, int n)
{
	if(n != 1)
		return -1;
	uart->stop = n;
	return 0;
}

static int
parity(Uart *uart, int n)
{
	if(n != 'n')
		return -1;
	uart->parity = n;
	return 0;
}

/*
 * cts/rts flow control
 *   need to bring signals to gpio pins before enabling this
 */

static void
modemctl(Uart *uart, int on)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(on)
		ap[MuCntl] |= CtsFlow;
	else
		ap[MuCntl] &= ~CtsFlow;
	uart->modem = on;
}

static void
rts(Uart *uart, int on)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(on)
		ap[MuMcr] &= ~RtsN;
	else
		ap[MuMcr] |= RtsN;
}

static long
status(Uart *uart, void *buf, long n, long offset)
{
	char *p;

	p = malloc(READSTR);
	if(p == nil)
		error(Enomem);
	snprint(p, READSTR,
		"b%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)\n",

		uart->baud,
		uart->dev,
		uart->type,
		uart->ferr,
		uart->oerr,
		uart->berr,
		uart->serr
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static void
donothing(Uart*, int)
{
}

void
putc(Uart*, int c)
{
	u32int *ap;

	ap = (u32int*)AUXREGS;
	while((ap[MuLsr] & TxRdy) == 0)
		;
	ap[MuIo] = c;
	while((ap[MuLsr] & TxRdy) == 0)
		;
}

int
getc(Uart*)
{
	u32int *ap;

	ap = (u32int*)AUXREGS;
	while((ap[MuLsr] & RxRdy) == 0)
		;
	return ap[MuIo] & 0xFF;
}

void
uartconsinit(void)
{
	Uart *uart;
	int n;
	char *p, *cmd;

	if((p = getconf("console")) == nil)
		return;
	n = strtoul(p, &cmd, 0);
	if(p == cmd)
		return;
	switch(n){
	default:
		return;
	case 0:
		uart = &miniuart;
		break;
	}

	if(!uart->enabled)
		(*uart->phys->enable)(uart, 0);
	uartctl(uart, "l8 pn s1");
	if(*cmd != '\0')
		uartctl(uart, cmd);

	consuart = uart;
	uart->console = 1;
}

PhysUart miniphysuart = {
	.name		= "miniuart",
	.pnp		= pnp,
	.enable		= enable,
	.disable	= disable,
	.kick		= kick,
	.dobreak	= dobreak,
	.baud		= baud,
	.bits		= bits,
	.stop		= stop,
	.parity		= parity,
	.modemctl	= donothing,
	.rts		= rts,
	.dtr		= donothing,
	.status		= status,
	.fifo		= donothing,
	.getc		= getc,
	.putc		= putc,
};

void
okay(int on)
{
	static int first;
	static int okled, polarity;
	char *p;

	if(!first++){
		p = getconf("bcm2709.disk_led_gpio");
		if(p == nil)
			p = getconf("bcm2708.disk_led_gpio");
		if(p != nil)
			okled = strtol(p, 0, 0);
		else
			okled = 'v';
		p = getconf("bcm2709.disk_led_active_low");
		if(p == nil)
			p = getconf("bcm2708.disk_led_active_low");
		polarity = (p == nil || *p == '1');
		if(okled != 'v')
			gpiosel(okled, Output);
	}
	if(okled == 'v')
		vgpset(0, on);
	else if(okled != 0)
		gpioout(okled, on^polarity);
}
", NDebug);
			else{
				iprint("kernel bkpt: pc %#lux inst %#ux\n",
					va, *(u32int*)va);
				panic("kernel bkpt");
			}
			break;
		default:
			faultarm(ureg, va, user, 1);
			break;
		}
		brusbdwc.c                                                                                               664       0       0        46767 13261231023  10702                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * USB host driver for BCM2835
 *	Synopsis DesignWare Core USB 2.0 OTG controller
 *
 * Copyright © 2012 Richard Miller <r.miller@acm.org>
 *
 * This is work in progress:
 * - no isochronous pipes
 * - no bandwidth budgeting
 * - frame scheduling is crude
 * - error handling is overly optimistic
 * It should be just about adequate for a Plan 9 terminal with
 * keyboard, mouse, ethernet adapter, and an external flash drive.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"

#include "dwcotg.h"

enum
{
	USBREGS		= VIRTIO + 0x980000,
	Enabledelay	= 50,
	Resetdelay	= 10,
	ResetdelayHS	= 50,

	Read		= 0,
	Write		= 1,

	/*
	 * Workaround for an unexplained glitch where an Ack interrupt
	 * is received without Chhltd, whereupon all channels remain
	 * permanently busy and can't be halted.  This was only seen
	 * when the controller is reading a sequence of bulk input
	 * packets in DMA mode.  Setting Slowbulkin=1 will avoid the
	 * lockup by reading packets individually with an interrupt
	 * after each.  More recent chips don't seem to exhibit the
	 * problem, so it's probably safe to leave this off now.
	 */
	Slowbulkin	= 0,
};

typedef struct Ctlr Ctlr;
typedef struct Epio Epio;

struct Ctlr {
	Lock;
	Dwcregs	*regs;		/* controller registers */
	int	nchan;		/* number of host channels */
	ulong	chanbusy;	/* bitmap of in-use channels */
	QLock	chanlock;	/* serialise access to chanbusy */
	QLock	split;		/* serialise split transactions */
	int	splitretry;	/* count retries of Nyet */
	int	sofchan;	/* bitmap of channels waiting for sof */
	int	wakechan;	/* bitmap of channels to wakeup after fiq */
	int	debugchan;	/* bitmap of channels for interrupt debug */
	Rendez	*chanintr;	/* sleep till interrupt on channel N */
};

struct Epio {
	union {
		QLock	rlock;
		QLock	ctllock;
	};
	QLock	wlock;
	Block	*cb;
	ulong	lastpoll;
};

static Ctlr dwc;
static int debug;

static char Ebadlen[] = "bad usb request length";

static void clog(Ep *ep, Hostchan *hc);
static void logdump(Ep *ep);

static void
filock(Lock *l)
{
	int x;

	x = splfhi();
	ilock(l);
	l->sr = x;
}

static void
fiunlock(Lock *l)
{
	iunlock(l);
}

static Hostchan*
chanalloc(Ep *ep)
{
	Ctlr *ctlr;
	int bitmap, i;
	static int first;

	ctlr = ep->hp->aux;
retry:
	qlock(&ctlr->chanlock);
	bitmap = ctlr->chanbusy;
	for(i = 0; i < ctlr->nchan; i++)
		if((bitmap & (1<<i)) == 0){
			ctlr->chanbusy = bitmap | 1<<i;
			qunlock(&ctlr->chanlock);
			return &ctlr->regs->hchan[i];
		}
	qunlock(&ctlr->chanlock);
	if(!first++)
		print("usbdwc: all host channels busy - retrying\n");
	tsleep(&up->sleep, return0, 0, 1);
	goto retry;
}

static void
chanrelease(Ep *ep, Hostchan *chan)
{
	Ctlr *ctlr;
	int i;

	ctlr = ep->hp->aux;
	i = chan - ctlr->regs->hchan;
	qlock(&ctlr->chanlock);
	ctlr->chanbusy &= ~(1<<i);
	qunlock(&ctlr->chanlock);
}

static void
chansetup(Hostchan *hc, Ep *ep)
{
	int hcc;
	Ctlr *ctlr = ep->hp->aux;

	if(ep->debug)
		ctlr->debugchan |= 1 << (hc - ctlr->regs->hchan);
	else
		ctlr->debugchan &= ~(1 << (hc - ctlr->regs->hchan));
	switch(ep->dev->state){
	case Dconfig:
	case Dreset:
		hcc = 0;
		break;
	default:
		hcc = ep->dev->nb<<ODevaddr;
		break;
	}
	hcc |= ep->maxpkt | 1<<OMulticnt | ep->nb<<OEpnum;
	switch(ep->ttype){
	case Tctl:
		hcc |= Epctl;
		break;
	case Tiso:
		hcc |= Episo;
		break;
	case Tbulk:
		hcc |= Epbulk;
		break;
	case Tintr:
		hcc |= Epintr;
		break;
	}
	switch(ep->dev->speed){
	case Lowspeed:
		hcc |= Lspddev;
		/* fall through */
	case Fullspeed:
		if(ep->dev->hub > 1){
			hc->hcsplt = Spltena | POS_ALL | ep->dev->hub<<OHubaddr |
				ep->dev->port;
			break;
		}
		/* fall through */
	default:
		hc->hcsplt = 0;
		break;
	}
	hc->hcchar = hcc;
	hc->hcint = ~0;
}

static int
sofdone(void *a)
{
	Dwcregs *r;

	r = a;
	return (r->gintmsk & Sofintr) == 0;
}

static void
sofwait(Ctlr *ctlr, int n)
{
	Dwcregs *r;

	r = ctlr->regs;
	do{
		filock(ctlr);
		r->gintsts = Sofintr;
		ctlr->sofchan |= 1<<n;
		r->gintmsk |= Sofintr;
		fiunlock(ctlr);
		sleep(&ctlr->chanintr[n], sofdone, r);
	}while((r->hfnum & 7) == 6);
}

static int
chandone(void *a)
{
	Hostchan *hc;

	hc = a;
	if(hc->hcint == (Chhltd|Ack))
		return 0;
	return (hc->hcint & hc->hcintmsk) != 0;
}

static int
chanwait(Ep *ep, Ctlr *ctlr, Hostchan *hc, int mask)
{
	int intr, n, ointr;
	ulong start, now;
	Dwcregs *r;

	r = ctlr->regs;
	n = hc - r->hchan;
	for(;;){
restart:
		filock(ctlr);
		r->haintmsk |= 1<<n;
		hc->hcintmsk = mask;
		fiunlock(ctlr);
		tsleep(&ctlr->chanintr[n], chandone, hc, 1000);
		if((intr = hc->hcint) == 0)
			goto restart;
		hc->hcintmsk = 0;
		if(intr & Chhltd)
			return intr;
		start = fastticks(0);
		ointr = intr;
		now = start;
		do{
			intr = hc->hcint;
			if(intr & Chhltd){
				if((ointr != Ack && ointr != (Ack|Xfercomp)) ||
				   intr != (Ack|Chhltd|Xfercomp) ||
				   (now - start) > 60)
					dprint("await %x after %ldµs %x -> %x\n",
						mask, now - start, ointr, intr);
				return intr;
			}
			if((intr & mask) == 0){
				if(intr != Nak)
					dprint("ep%d.%d await %x after %ldµs intr %x -> %x\n",
						ep->dev->nb, ep->nb, mask, now - start, ointr, intr);
				goto restart;
			}
			now = fastticks(0);
		}while(now - start < 100);
		dprint("ep%d.%d halting channel %8.8ux hcchar %8.8ux "
			"grxstsr %8.8ux gnptxsts %8.8ux hptxsts %8.8ux\n",
			ep->dev->nb, ep->nb, intr, hc->hcchar, r->grxstsr,
			r->gnptxsts, r->hptxsts);
		mask = Chhltd;
		hc->hcchar |= Chdis;
		start = m->ticks;
		while(hc->hcchar & Chen){
			if(m->ticks - start >= 100){
				print("ep%d.%d channel won't halt hcchar %8.8ux\n",
					ep->dev->nb, ep->nb, hc->hcchar);
				break;
			}
		}
		logdump(ep);
	}
}

static int
chanintr(Ctlr *ctlr, int n)
{
	Hostchan *hc;
	int i;

	hc = &ctlr->regs->hchan[n];
	if((hc->hcint & hc->hcintmsk) == 0)
		return 1;
	if(ctlr->debugchan & (1<<n))
		clog(nil, hc);
	if((hc->hcsplt & Spltena) == 0)
		return 0;
	i = hc->hcint;
	if(i == (Chhltd|Ack)){
		hc->hcsplt |= Compsplt;
		ctlr->splitretry = 0;
	}else if(i == (Chhltd|Nyet)){
		if(++ctlr->splitretry >= 3)
			return 0;
	}else
		return 0;
	if(hc->hcchar & Chen){
		iprint("hcchar %8.8ux hcint %8.8ux", hc->hcchar, hc->hcint);
		hc->hcchar |= Chen | Chdis;
		while(hc->hcchar&Chen)
			;
		iprint(" %8.8ux\n", hc->hcint);
	}
	hc->hcint = i;
	if(ctlr->regs->hfnum & 1)
		hc->hcchar &= ~Oddfrm;
	else
		hc->hcchar |= Oddfrm;
	hc->hcchar = (hc->hcchar &~ Chdis) | Chen;
	return 1;
}

static Reg chanlog[32][5];
static int nchanlog;

static void
logstart(Ep *ep)
{
	if(ep->debug)
		nchanlog = 0;
}

static void
clog(Ep *ep, Hostchan *hc)
{
	Reg *p;

	if(ep != nil && !ep->debug)
		return;
	if(nchanlog == 32)
		nchanlog--;
	p = chanlog[nchanlog];
	p[0] = dwc.regs->hfnum;
	p[1] = hc->hcchar;
	p[2] = hc->hcint;
	p[3] = hc->hctsiz;
	p[4] = hc->hcdma;
	nchanlog++;
}

static void
logdump(Ep *ep)
{
	Reg *p;
	int i;

	if(!ep->debug)
		return;
	p = chanlog[0];
	for(i = 0; i < nchanlog; i++){
		print("%5.5d.%5.5d %8.8ux %8.8ux %8.8ux %8.8ux\n",
			p[0]&0xFFFF, p[0]>>16, p[1], p[2], p[3], p[4]);
		p += 5;
	}
	nchanlog = 0;
}

static int
chanio(Ep *ep, Hostchan *hc, int dir, int pid, void *a, int len)
{
	Ctlr *ctlr;
	int nleft, n, nt, i, maxpkt, npkt;
	uint hcdma, hctsiz;

	ctlr = ep->hp->aux;
	maxpkt = ep->maxpkt;
	npkt = HOWMANY(len, ep->maxpkt);
	if(npkt == 0)
		npkt = 1;

	hc->hcchar = (hc->hcchar & ~Epdir) | dir;
	if(dir == Epin)
		n = ROUND(len, ep->maxpkt);
	else
		n = len;
	hc->hctsiz = n | npkt<<OPktcnt | pid;
	hc->hcdma  = dmaaddr(a);

	nleft = len;
	logstart(ep);
	for(;;){
		hcdma = hc->hcdma;
		hctsiz = hc->hctsiz;
		hc->hctsiz = hctsiz & ~Dopng;
		if(hc->hcchar&Chen){
			dprint("ep%d.%d before chanio hcchar=%8.8ux\n",
				ep->dev->nb, ep->nb, hc->hcchar);
			hc->hcchar |= Chen | Chdis;
			while(hc->hcchar&Chen)
				;
			hc->hcint = Chhltd;
		}
		if((i = hc->hcint) != 0){
			dprint("ep%d.%d before chanio hcint=%8.8ux\n",
				ep->dev->nb, ep->nb, i);
			hc->hcint = i;
		}
		if(hc->hcsplt & Spltena){
			qlock(&ctlr->split);
			sofwait(ctlr, hc - ctlr->regs->hchan);
			if((dwc.regs->hfnum & 1) == 0)
				hc->hcchar &= ~Oddfrm;
			else
				hc->hcchar |= Oddfrm;
		}
		hc->hcchar = (hc->hcchar &~ Chdis) | Chen;
		clog(ep, hc);
wait:
		if(ep->ttype == Tbulk && dir == Epin)
			i = chanwait(ep, ctlr, hc, Chhltd);
		else if(ep->ttype == Tintr && (hc->hcsplt & Spltena))
			i = chanwait(ep, ctlr, hc, Chhltd);
		else
			i = chanwait(ep, ctlr, hc, Chhltd|Nak);
		clog(ep, hc);
		if(hc->hcint != i){
			dprint("chanwait intr %ux->%ux\n", i, hc->hcint);
			if((i = hc->hcint) == 0)
				goto wait;
		}
		hc->hcint = i;

		if(hc->hcsplt & Spltena){
			hc->hcsplt &= ~Compsplt;
			qunlock(&ctlr->split);
		}

		if((i & Xfercomp) == 0 && i != (Chhltd|Ack) && i != Chhltd){
			if(i & Stall)
				error(Estalled);
			if(i & (Nyet|Frmovrun))
				continue;
			if(i & Nak){
				if(ep->ttype == Tintr)
					tsleep(&up->sleep, return0, 0, ep->pollival);
				else
					tsleep(&up->sleep, return0, 0, 1);
				continue;
			}
			logdump(ep);
			print("usbdwc: ep%d.%d error intr %8.8ux\n",
				ep->dev->nb, ep->nb, i);
			if(i & ~(Chhltd|Ack))
				error(Eio);
			if(hc->hcdma != hcdma)
				print("usbdwc: weird hcdma %ux->%ux intr %ux->%ux\n",
					hcdma, hc->hcdma, i, hc->hcint);
		}
		n = hc->hcdma - hcdma;
		if(n == 0){
			if((hc->hctsiz & Pktcnt) != (hctsiz & Pktcnt))
				break;
			else
				continue;
		}
		if(dir == Epin && ep->ttype == Tbulk){
			nt = (hctsiz & Xfersize) - (hc->hctsiz & Xfersize);
			if(nt != n){
				if(n == ROUND(nt, 4))
					n = nt;
				else
					print("usbdwc: intr %8.8ux "
						"dma %8.8ux-%8.8ux "
						"hctsiz %8.8ux-%8.ux\n",
						i, hcdma, hc->hcdma, hctsiz,
						hc->hctsiz);
			}
		}
		if(n > nleft){
			if(n != ROUND(nleft, 4))
				dprint("too much: wanted %d got %d\n",
					len, len - nleft + n);
			n = nleft;
		}
		nleft -= n;
		if(nleft == 0 || (n % maxpkt) != 0)
			break;
		if((i & Xfercomp) && ep->ttype != Tctl)
			break;
		if(dir == Epout)
			dprint("too little: nleft %d hcdma %x->%x hctsiz %x->%x intr %x\n",
				nleft, hcdma, hc->hcdma, hctsiz, hc->hctsiz, i);
	}
	logdump(ep);
	return len - nleft;
}

static long
multitrans(Ep *ep, Hostchan *hc, int rw, void *a, long n)
{
	long sofar, m;

	sofar = 0;
	do{
		m = n - sofar;
		if(m > ep->maxpkt)
			m = ep->maxpkt;
		m = chanio(ep, hc, rw == Read? Epin : Epout, ep->toggle[rw],
			(char*)a + sofar, m);
		ep->toggle[rw] = hc->hctsiz & Pid;
		sofar += m;
	}while(sofar < n && m == ep->maxpkt);
	return sofar;
}

static long
eptrans(Ep *ep, int rw, void *a, long n)
{
	Hostchan *hc;

	if(ep->clrhalt){
		ep->clrhalt = 0;
		if(ep->mode != OREAD)
			ep->toggle[Write] = DATA0;
		if(ep->mode != OWRITE)
			ep->toggle[Read] = DATA0;
	}
	hc = chanalloc(ep);
	if(waserror()){
		ep->toggle[rw] = hc->hctsiz & Pid;
		chanrelease(ep, hc);
		if(strcmp(up->errstr, Estalled) == 0)
			return 0;
		nexterror();
	}
	chansetup(hc, ep);
	if(Slowbulkin && rw == Read && ep->ttype == Tbulk)
		n = multitrans(ep, hc, rw, a, n);
	else{
		n = chanio(ep, hc, rw == Read? Epin : Epout, ep->toggle[rw],
			a, n);
		ep->toggle[rw] = hc->hctsiz & Pid;
	}
	chanrelease(ep, hc);
	poperror();
	return n;
}

static long
ctltrans(Ep *ep, uchar *req, long n)
{
	Hostchan *hc;
	Epio *epio;
	Block *b;
	uchar *data;
	int datalen;

	epio = ep->aux;
	if(epio->cb != nil){
		freeb(epio->cb);
		epio->cb = nil;
	}
	if(n < Rsetuplen)
		error(Ebadlen);
	if(req[Rtype] & Rd2h){
		datalen = GET2(req+Rcount);
		if(datalen <= 0 || datalen > Maxctllen)
			error(Ebadlen);
		/* XXX cache madness */
		epio->cb = b = allocb(ROUND(datalen, ep->maxpkt));
		assert(((uintptr)b->wp & (BLOCKALIGN-1)) == 0);
		memset(b->wp, 0x55, b->lim - b->wp);
		cachedwbinvse(b->wp, b->lim - b->wp);
		data = b->wp;
	}else{
		b = nil;
		datalen = n - Rsetuplen;
		data = req + Rsetuplen;
	}
	hc = chanalloc(ep);
	if(waserror()){
		chanrelease(ep, hc);
		if(strcmp(up->errstr, Estalled) == 0)
			return 0;
		nexterror();
	}
	chansetup(hc, ep);
	chanio(ep, hc, Epout, SETUP, req, Rsetuplen);
	if(req[Rtype] & Rd2h){
		if(ep->dev->hub <= 1){
			ep->toggle[Read] = DATA1;
			b->wp += multitrans(ep, hc, Read, data, datalen);
		}else
			b->wp += chanio(ep, hc, Epin, DATA1, data, datalen);
		chanio(ep, hc, Epout, DATA1, nil, 0);
		cachedinvse(b->rp, BLEN(b));
		n = Rsetuplen;
	}else{
		if(datalen > 0)
			chanio(ep, hc, Epout, DATA1, data, datalen);
		chanio(ep, hc, Epin, DATA1, nil, 0);
		n = Rsetuplen + datalen;
	}
	chanrelease(ep, hc);
	poperror();
	return n;
}

static long
ctldata(Ep *ep, void *a, long n)
{
	Epio *epio;
	Block *b;

	epio = ep->aux;
	b = epio->cb;
	if(b == nil)
		return 0;
	if(n > BLEN(b))
		n = BLEN(b);
	memmove(a, b->rp, n);
	b->rp += n;
	if(BLEN(b) == 0){
		freeb(b);
		epio->cb = nil;
	}
	return n;
}

static void
greset(Dwcregs *r, int bits)
{
	r->grstctl |= bits;
	while(r->grstctl & bits)
		;
	microdelay(10);
}

static void
init(Hci *hp)
{
	Ctlr *ctlr;
	Dwcregs *r;
	uint n, rx, tx, ptx;

	ctlr = hp->aux;
	r = ctlr->regs;

	ctlr->nchan = 1 + ((r->ghwcfg2 & Num_host_chan) >> ONum_host_chan);
	ctlr->chanintr = malloc(ctlr->nchan * sizeof(Rendez));

	r->gahbcfg = 0;
	setpower(PowerUsb, 1);

	while((r->grstctl&Ahbidle) == 0)
		;
	greset(r, Csftrst);

	r->gusbcfg |= Force_host_mode;
	tsleep(&up->sleep, return0, 0, 25);
	r->gahbcfg |= Dmaenable;

	n = (r->ghwcfg3 & Dfifo_depth) >> ODfifo_depth;
	rx = 0x306;
	tx = 0x100;
	ptx = 0x200;
	r->grxfsiz = rx;
	r->gnptxfsiz = rx | tx<<ODepth;
	tsleep(&up->sleep, return0, 0, 1);
	r->hptxfsiz = (rx + tx) | ptx << ODepth;
	greset(r, Rxfflsh);
	r->grstctl = TXF_ALL;
	greset(r, Txfflsh);
	dprint("usbdwc: FIFO depth %d sizes rx/nptx/ptx %8.8ux %8.8ux %8.8ux\n",
		n, r->grxfsiz, r->gnptxfsiz, r->hptxfsiz);

	r->hport0 = Prtpwr|Prtconndet|Prtenchng|Prtovrcurrchng;
	r->gintsts = ~0;
	r->gintmsk = Hcintr;
	r->gahbcfg |= Glblintrmsk;
}

static void
dump(Hci*)
{
}

static void
fiqintr(Ureg*, void *a)
{
	Hci *hp;
	Ctlr *ctlr;
	Dwcregs *r;
	uint intr, haint, wakechan;
	int i;

	hp = a;
	ctlr = hp->aux;
	r = ctlr->regs;
	wakechan = 0;
	filock(ctlr);
	intr = r->gintsts;
	if(intr & Hcintr){
		haint = r->haint & r->haintmsk;
		for(i = 0; haint; i++){
			if(haint & 1){
				if(chanintr(ctlr, i) == 0){
					r->haintmsk &= ~(1<<i);
					wakechan |= 1<<i;
				}
			}
			haint >>= 1;
		}
	}
	if(intr & Sofintr){
		r->gintsts = Sofintr;
		if((r->hfnum&7) != 6){
			r->gintmsk &= ~Sofintr;
			wakechan |= ctlr->sofchan;
			ctlr->sofchan = 0;
		}
	}
	if(wakechan){
		ctlr->wakechan |= wakechan;
		armtimerset(1);
	}
	fiunlock(ctlr);
}

static void
irqintr(Ureg*, void *a)
{
	Ctlr *ctlr;
	uint wakechan;
	int i;

	ctlr = a;
	filock(ctlr);
	armtimerset(0);
	wakechan = ctlr->wakechan;
	ctlr->wakechan = 0;
	fiunlock(ctlr);
	for(i = 0; wakechan; i++){
		if(wakechan & 1)
			wakeup(&ctlr->chanintr[i]);
		wakechan >>= 1;
	}
}

static void
epopen(Ep *ep)
{
	ddprint("usbdwc: epopen ep%d.%d ttype %d\n",
		ep->dev->nb, ep->nb, ep->ttype);
	switch(ep->ttype){
	case Tnone:
		error(Enotconf);
	case Tintr:
		assert(ep->pollival > 0);
		/* fall through */
	case Tbulk:
		if(ep->toggle[Read] == 0)
			ep->toggle[Read] = DATA0;
		if(ep->toggle[Write] == 0)
			ep->toggle[Write] = DATA0;
		break;
	}
	ep->aux = malloc(sizeof(Epio));
	if(ep->aux == nil)
		error(Enomem);
}

static void
epclose(Ep *ep)
{
	ddprint("usbdwc: epclose ep%d.%d ttype %d\n",
		ep->dev->nb, ep->nb, ep->ttype);
	switch(ep->ttype){
	case Tctl:
		freeb(((Epio*)ep->aux)->cb);
		/* fall through */
	default:
		free(ep->aux);
		break;
	}
}

static long
epread(Ep *ep, void *a, long n)
{
	Epio *epio;
	QLock *q;
	Block *b;
	uchar *p;
	ulong elapsed;
	long nr;

	ddprint("epread ep%d.%d %ld\n", ep->dev->nb, ep->nb, n);
	epio = ep->aux;
	q = ep->ttype == Tctl? &epio->ctllock : &epio->rlock;
	b = nil;
	qlock(q);
	if(waserror()){
		qunlock(q);
		if(b)
			freeb(b);
		nexterror();
	}
	switch(ep->ttype){
	default:
		error(Egreg);
	case Tctl:
		nr = ctldata(ep, a, n);
		qunlock(q);
		poperror();
		return nr;
	case Tintr:
		elapsed = TK2MS(m->ticks) - epio->lastpoll;
		if(elapsed < ep->pollival)
			tsleep(&up->sleep, return0, 0, ep->pollival - elapsed);
		/* fall through */
	case Tbulk:
		/* XXX cache madness */
		b = allocb(ROUND(n, ep->maxpkt));
		p = b->rp;
		assert(((uintptr)p & (BLOCKALIGN-1)) == 0);
		cachedinvse(p, n);
		nr = eptrans(ep, Read, p, n);
		cachedinvse(p, nr);
		epio->lastpoll = TK2MS(m->ticks);
		memmove(a, p, nr);
		qunlock(q);
		freeb(b);
		poperror();
		return nr;
	}
}

static long
epwrite(Ep *ep, void *a, long n)
{
	Epio *epio;
	QLock *q;
	Block *b;
	uchar *p;
	ulong elapsed;

	ddprint("epwrite ep%d.%d %ld\n", ep->dev->nb, ep->nb, n);
	epio = ep->aux;
	q = ep->ttype == Tctl? &epio->ctllock : &epio->wlock;
	b = nil;
	qlock(q);
	if(waserror()){
		qunlock(q);
		if(b)
			freeb(b);
		nexterror();
	}
	switch(ep->ttype){
	default:
		error(Egreg);
	case Tintr:
		elapsed = TK2MS(m->ticks) - epio->lastpoll;
		if(elapsed < ep->pollival)
			tsleep(&up->sleep, return0, 0, ep->pollival - elapsed);
		/* fall through */
	case Tctl:
	case Tbulk:
		/* XXX cache madness */
		b = allocb(n);
		p = b->wp;
		assert(((uintptr)p & (BLOCKALIGN-1)) == 0);
		memmove(p, a, n);
		cachedwbse(p, n);
		if(ep->ttype == Tctl)
			n = ctltrans(ep, p, n);
		else{
			n = eptrans(ep, Write, p, n);
			epio->lastpoll = TK2MS(m->ticks);
		}
		qunlock(q);
		freeb(b);
		poperror();
		return n;
	}
}

static char*
seprintep(char *s, char*, Ep*)
{
	return s;
}
	
static int
portenable(Hci *hp, int port, int on)
{
	Ctlr *ctlr;
	Dwcregs *r;

	assert(port == 1);
	ctlr = hp->aux;
	r = ctlr->regs;
	dprint("usbdwc enable=%d; sts %#x\n", on, r->hport0);
	if(!on)
		r->hport0 = Prtpwr | Prtena;
	tsleep(&up->sleep, return0, 0, Enabledelay);
	dprint("usbdwc enable=%d; sts %#x\n", on, r->hport0);
	return 0;
}

static int
portreset(Hci *hp, int port, int on)
{
	Ctlr *ctlr;
	Dwcregs *r;
	int b, s;

	assert(port == 1);
	ctlr = hp->aux;
	r = ctlr->regs;
	dprint("usbdwc reset=%d; sts %#x\n", on, r->hport0);
	if(!on)
		return 0;
	r->hport0 = Prtpwr | Prtrst;
	tsleep(&up->sleep, return0, 0, ResetdelayHS);
	r->hport0 = Prtpwr;
	tsleep(&up->sleep, return0, 0, Enabledelay);
	s = r->hport0;
	b = s & (Prtconndet|Prtenchng|Prtovrcurrchng);
	if(b != 0)
		r->hport0 = Prtpwr | b;
	dprint("usbdwc reset=%d; sts %#x\n", on, s);
	if((s & Prtena) == 0)
		print("usbdwc: host port not enabled after reset");
	return 0;
}

static int
portstatus(Hci *hp, int port)
{
	Ctlr *ctlr;
	Dwcregs *r;
	int b, s;

	assert(port == 1);
	ctlr = hp->aux;
	r = ctlr->regs;
	s = r->hport0;
	b = s & (Prtconndet|Prtenchng|Prtovrcurrchng);
	if(b != 0)
		r->hport0 = Prtpwr | b;
	b = 0;
	if(s & Prtconnsts)
		b |= HPpresent;
	if(s & Prtconndet)
		b |= HPstatuschg;
	if(s & Prtena)
		b |= HPenable;
	if(s & Prtenchng)
		b |= HPchange;
	if(s & Prtovrcurract)
		 b |= HPovercurrent;
	if(s & Prtsusp)
		b |= HPsuspend;
	if(s & Prtrst)
		b |= HPreset;
	if(s & Prtpwr)
		b |= HPpower;
	switch(s & Prtspd){
	case HIGHSPEED:
		b |= HPhigh;
		break;
	case LOWSPEED:
		b |= HPslow;
		break;
	}
	return b;
}

static void
shutdown(Hci*)
{
}

static void
setdebug(Hci*, int d)
{
	debug = d;
}

static int
reset(Hci *hp)
{
	Ctlr *ctlr;
	uint id;

	ctlr = &dwc;
	if(ctlr->regs != nil)
		return -1;
	ctlr->regs = (Dwcregs*)USBREGS;
	id = ctlr->regs->gsnpsid;
	if((id>>16) != ('O'<<8 | 'T'))
		return -1;
	dprint("usbdwc: rev %d.%3.3x\n", (id>>12)&0xF, id&0xFFF);

	intrenable(IRQtimerArm, irqintr, ctlr, 0, "dwc");

	hp->aux = ctlr;
	hp->port = 0;
	hp->irq = IRQusb;
	hp->tbdf = 0;
	hp->nports = 1;
	hp->highspeed = 1;

	hp->init = init;
	hp->dump = dump;
	hp->interrupt = fiqintr;
	hp->epopen = epopen;
	hp->epclose = epclose;
	hp->epread = epread;
	hp->epwrite = epwrite;
	hp->seprintep = seprintep;
	hp->portenable = portenable;
	hp->portreset = portreset;
	hp->portstatus = portstatus;
	hp->shutdown = shutdown;
	hp->debug = setdebug;
	hp->type = "dwcotg";
	return 0;
}

void
usbdwclink(void)
{
	addhcitype("dwcotg", reset);
}
		hc->hctvcore.c                                                                                                664       0       0        14361 13530000372  10512                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    #include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * Mailbox interface with videocore gpu
 */

#define	MAILBOX		(VIRTIO+0xB880)

typedef struct Prophdr Prophdr;
typedef struct Fbinfo Fbinfo;
typedef struct Vgpio Vgpio;

enum {
	Read		= 0x00>>2,
	Write		= 0x00>>2,
	Peek		= 0x10>>2,
	Sender		= 0x14>>2,
	Status		= 0x18>>2,
		Full		= 1<<31,
		Empty		= 1<<30,
	Config		= 0x1C>>2,
	NRegs		= 0x20>>2,

	ChanMask	= 0xF,
	ChanProps	= 8,
	ChanFb		= 1,

	Req			= 0x0,
	RspOk		= 0x80000000,
	TagResp		= 1<<31,

	TagGetfwrev	= 0x00000001,
	TagGetrev	= 0x00010002,
	TagGetmac	= 0x00010003,
	TagGetram	= 0x00010005,
	TagGetpower	= 0x00020001,
	TagSetpower	= 0x00028001,
		Powerwait	= 1<<1,
	TagGetclkspd= 0x00030002,
	TagGetclkmax= 0x00030004,
	TagSetclkspd= 0x00038002,
	TagGettemp	= 0x00030006,
	TagFballoc	= 0x00040001,
	TagFbfree	= 0x00048001,
	TagFbblank	= 0x00040002,
	TagGetres	= 0x00040003,
	TagSetres	= 0x00048003,
	TagGetvres	= 0x00040004,
	TagSetvres	= 0x00048004,
	TagGetdepth	= 0x00040005,
	TagSetdepth	= 0x00048005,
	TagGetrgb	= 0x00040006,
	TagSetrgb	= 0x00048006,
	TagGetGpio	= 0x00040010,

	Nvgpio		= 2,
};

struct Fbinfo {
	u32int	xres;
	u32int	yres;
	u32int	xresvirtual;
	u32int	yresvirtual;
	u32int	pitch;			/* returned by gpu */
	u32int	bpp;
	u32int	xoffset;
	u32int	yoffset;
	u32int	base;			/* returned by gpu */
	u32int	screensize;		/* returned by gpu */
};


struct Prophdr {
	u32int	len;
	u32int	req;
	u32int	tag;
	u32int	tagbuflen;
	u32int	taglen;
	u32int	data[1];
};

struct Vgpio {
	u32int	*counts;
	u16int	incs;
	u16int	decs;
	int	ison;
};

static Vgpio vgpio;

static void
vcwrite(uint chan, int val)
{
	u32int *r;

	r = (u32int*)MAILBOX + NRegs;
	val &= ~ChanMask;
	while(r[Status]&Full)
		;
	coherence();
	r[Write] = val | chan;
}

static int
vcread(uint chan)
{
	u32int *r;
	int x;

	r = (u32int*)MAILBOX;
	do{
		while(r[Status]&Empty)
			;
		coherence();
		x = r[Read];
	}while((x&ChanMask) != chan);
	return x & ~ChanMask;
}

/*
 * Property interface
 */

static int
vcreq(int tag, void *buf, int vallen, int rsplen)
{
	uintptr r;
	int n;
	Prophdr *prop;
	uintptr aprop;
	static int busaddr = 1;

	if(rsplen < vallen)
		rsplen = vallen;
	rsplen = (rsplen+3) & ~3;
	prop = (Prophdr*)(VCBUFFER);
	n = sizeof(Prophdr) + rsplen + 8;
	memset(prop, 0, n);
	prop->len = n;
	prop->req = Req;
	prop->tag = tag;
	prop->tagbuflen = rsplen;
	prop->taglen = vallen;
	if(vallen > 0)
		memmove(prop->data, buf, vallen);
	cachedwbinvse(prop, prop->len);
	for(;;){
		aprop = busaddr? dmaaddr(prop) : PTR2UINT(prop);
		vcwrite(ChanProps, aprop);
		r = vcread(ChanProps);
		if(r == aprop)
			break;
		if(!busaddr)
			return -1;
		busaddr = 0;
	}
	if(prop->req == RspOk &&
	   prop->tag == tag &&
	   (prop->taglen&TagResp)) {
		if((n = prop->taglen & ~TagResp) < rsplen)
			rsplen = n;
		memmove(buf, prop->data, rsplen);
	}else
		rsplen = -1;

	return rsplen;
}

/*
 * Framebuffer
 */

static int
fbdefault(int *width, int *height, int *depth)
{
	u32int buf[3];
	char *p;

	if(vcreq(TagGetres, &buf[0], 0, 2*4) != 2*4 ||
	   vcreq(TagGetdepth, &buf[2], 0, 4) != 4)
		return -1;
	*width = buf[0];
	*height = buf[1];
	if((p = getconf("bcm2708_fb.fbdepth")) != nil)
		*depth = atoi(p);
	else
		*depth = buf[2];
	return 0;
}

void*
fbinit(int set, int *width, int *height, int *depth)
{
	Fbinfo *fi;
	uintptr va;

	if(!set)
		fbdefault(width, height, depth);
	/* Screen width must be a multiple of 16 */
	*width &= ~0xF;
	fi = (Fbinfo*)(VCBUFFER);
	memset(fi, 0, sizeof(*fi));
	fi->xres = fi->xresvirtual = *width;
	fi->yres = fi->yresvirtual = *height;
	fi->bpp = *depth;
	cachedwbinvse(fi, sizeof(*fi));
	vcwrite(ChanFb, dmaaddr(fi));
	if(vcread(ChanFb) != 0)
		return 0;
	va = mmukmap(FRAMEBUFFER, fi->base & ~0xC0000000, fi->screensize);
	if(va)
		memset((char*)va, 0x7F, fi->screensize);
	return (void*)va;
}

int
fbblank(int blank)
{
	u32int buf[1];

	buf[0] = blank;
	if(vcreq(TagFbblank, buf, sizeof buf, sizeof buf) != sizeof buf)
		return -1;
	return buf[0] & 1;
}

/*
 * Power management
 */
void
setpower(int dev, int on)
{
	u32int buf[2];

	buf[0] = dev;
	buf[1] = Powerwait | (on? 1 : 0);
	vcreq(TagSetpower, buf, sizeof buf, sizeof buf);
}

int
getpower(int dev)
{
	u32int buf[2];

	buf[0] = dev;
	buf[1] = 0;
	if(vcreq(TagGetpower, buf, sizeof buf[0], sizeof buf) != sizeof buf)
		return -1;
	return buf[0] & 1;
}

/*
 * Get ethernet address (as hex string)
 *	 [not reentrant]
 */
char *
getethermac(void)
{
	uchar ea[8];
	char *p;
	int i;
	static char buf[16];

	memset(ea, 0, sizeof ea);
	vcreq(TagGetmac, ea, 0, sizeof ea);
	p = buf;
	for(i = 0; i < 6; i++)
		p += sprint(p, "%.2x", ea[i]);
	return buf;
}

/*
 * Get board revision
 */
uint
getboardrev(void)
{
	u32int buf[1];

	if(vcreq(TagGetrev, buf, 0, sizeof buf) != sizeof buf)
		return 0;
	return buf[0];
}

/*
 * Get firmware revision
 */
uint
getfirmware(void)
{
	u32int buf[1];

	if(vcreq(TagGetfwrev, buf, 0, sizeof buf) != sizeof buf)
		return 0;
	return buf[0];
}

/*
 * Get ARM ram
 */
void
getramsize(Confmem *mem)
{
	u32int buf[2];

	if(vcreq(TagGetram, buf, 0, sizeof buf) != sizeof buf)
		return;
	mem->base = buf[0];
	mem->limit = buf[1];
}

/*
 * Get clock rate
 */
ulong
getclkrate(int clkid)
{
	u32int buf[2];

	buf[0] = clkid;
	if(vcreq(TagGetclkspd, buf, sizeof(buf[0]), sizeof(buf)) != sizeof buf)
		return 0;
	return buf[1];
}

/*
 * Set clock rate to hz (or max speed if hz == 0)
 */
void
setclkrate(int clkid, ulong hz)
{
	u32int buf[2];

	buf[0] = clkid;
	if(hz != 0)
		buf[1] = hz;
	else if(vcreq(TagGetclkmax, buf, sizeof(buf[0]), sizeof(buf)) != sizeof buf)
		return;
	vcreq(TagSetclkspd, buf, sizeof(buf), sizeof(buf));
}

/*
 * Get cpu temperature
 */
uint
getcputemp(void)
{
	u32int buf[2];

	buf[0] = 0;
	if(vcreq(TagGettemp, buf, sizeof(buf[0]), sizeof buf) != sizeof buf)
		return 0;
	return buf[1];
}

/*
 * Virtual GPIO - used for ACT LED on pi3
 */
void
vgpinit(void)
{
	u32int buf[1];
	uintptr va;

	buf[0] = 0;
	if(vcreq(TagGetGpio, buf, 0, sizeof(buf)) != sizeof buf || buf[0] == 0)
		return;
	va = mmukmap(VGPIO, buf[0] & ~0xC0000000, BY2PG);
	if(va == 0)
		return;
	vgpio.counts = (u32int*)va;
}

void
vgpset(uint port, int on)
{
	if(vgpio.counts == nil || port >= Nvgpio || on == vgpio.ison)
		return;
	if(on)
		vgpio.incs++;
	else
		vgpio.decs++;
	vgpio.counts[port] = (vgpio.incs << 16) | vgpio.decs;
	vgpio.ison = on;
}
->dev->nb, ep->nb, n);
	epio = ep->aux;
	q = ep->ttype == Tctl? &epio->ctllock : &epio->wlock;
	b = nil;
	qlock(q);
	if(waserror()){
		qunlock(q);
		if(b)
			freeb(b);
		nexterror();
	}
	switch(ep->ttype){
	default:
		error(Egreg);
	case Tintr:
		elapsed = TK2MS(m->ticksvfp3.c                                                                                                 664       0       0        24675 12703250373  10274                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    /*
 * VFPv2 or VFPv3 floating point unit
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "arm.h"

/* subarchitecture code in m->havefp */
enum {
	VFPv2	= 2,
	VFPv3	= 3,
};

/* fp control regs.  most are read-only */
enum {
	Fpsid =	0,
	Fpscr =	1,			/* rw */
	Mvfr1 =	6,
	Mvfr0 =	7,
	Fpexc =	8,			/* rw */
	Fpinst= 9,			/* optional, for exceptions */
	Fpinst2=10,
};
enum {
	/* Fpexc bits */
	Fpex =		1u << 31,
	Fpenabled =	1 << 30,
	Fpdex =		1 << 29,	/* defined synch exception */
//	Fp2v =		1 << 28,	/* Fpinst2 reg is valid */
//	Fpvv =		1 << 27,	/* if Fpdex, vecitr is valid */
//	Fptfv = 	1 << 26,	/* trapped fault is valid */
//	Fpvecitr =	MASK(3) << 8,
	/* FSR bits appear here */
	Fpmbc =		Fpdex,		/* bits exception handler must clear */

	/* Fpscr bits; see u.h for more */
	Stride =	MASK(2) << 20,
	Len =		MASK(3) << 16,
	Dn=		1 << 25,
	Fz=		1 << 24,
	/* trap exception enables (not allowed in vfp3) */
	FPIDNRM =	1 << 15,	/* input denormal */
	Alltraps = FPIDNRM | FPINEX | FPUNFL | FPOVFL | FPZDIV | FPINVAL,
	/* pending exceptions */
	FPAIDNRM =	1 << 7,		/* input denormal */
	Allexc = FPAIDNRM | FPAINEX | FPAUNFL | FPAOVFL | FPAZDIV | FPAINVAL,
	/* condition codes */
	Allcc =		MASK(4) << 28,
};
enum {
	/* CpCPaccess bits */
	Cpaccnosimd =	1u << 31,
	Cpaccd16 =	1 << 30,
};

static char *
subarch(int impl, uint sa)
{
	static char *armarchs[] = {
		"VFPv1 (unsupported)",
		"VFPv2",
		"VFPv3+ with common VFP subarch v2",
		"VFPv3+ with null subarch",
		"VFPv3+ with common VFP subarch v3",
	};

	if (impl != 'A' || sa >= nelem(armarchs))
		return "GOK";
	else
		return armarchs[sa];
}

static char *
implement(uchar impl)
{
	if (impl == 'A')
		return "arm";
	else
		return "unknown";
}

static int
havefp(void)
{
	int gotfp;
	ulong acc, sid;

	if (m->havefpvalid)
		return m->havefp;

	m->havefp = 0;
	gotfp = 1 << CpFP | 1 << CpDFP;
	cpwrsc(0, CpCONTROL, 0, CpCPaccess, MASK(28));
	acc = cprdsc(0, CpCONTROL, 0, CpCPaccess);
	if ((acc & (MASK(2) << (2*CpFP))) == 0) {
		gotfp &= ~(1 << CpFP);
		print("fpon: no single FP coprocessor\n");
	}
	if ((acc & (MASK(2) << (2*CpDFP))) == 0) {
		gotfp &= ~(1 << CpDFP);
		print("fpon: no double FP coprocessor\n");
	}
	if (!gotfp) {
		print("fpon: no FP coprocessors\n");
		m->havefpvalid = 1;
		return 0;
	}
	m->fpon = 1;			/* don't panic */
	sid = fprd(Fpsid);
	m->fpon = 0;
	switch((sid >> 16) & MASK(7)){
	case 0:				/* VFPv1 */
		break;
	case 1:				/* VFPv2 */
		m->havefp = VFPv2;
		m->fpnregs = 16;
		break;
	default:			/* VFPv3 or later */
		m->havefp = VFPv3;
		m->fpnregs = (acc & Cpaccd16) ? 16 : 32;
		break;
	}
	if (m->machno == 0)
		print("fp: %d registers, %s simd\n", m->fpnregs,
			(acc & Cpaccnosimd? " no": ""));
	m->havefpvalid = 1;
	return 1;
}

/*
 * these can be called to turn the fpu on or off for user procs,
 * not just at system start up or shutdown.
 */

void
fpoff(void)
{
	if (m->fpon) {
		fpwr(Fpexc, 0);
		m->fpon = 0;
	}
}

void
fpononly(void)
{
	if (!m->fpon && havefp()) {
		/* enable fp.  must be first operation on the FPUs. */
		fpwr(Fpexc, Fpenabled);
		m->fpon = 1;
	}
}

static void
fpcfg(void)
{
	int impl;
	ulong sid;
	static int printed;

	/* clear pending exceptions; no traps in vfp3; all v7 ops are scalar */
	m->fpscr = Dn | FPRNR | (FPINVAL | FPZDIV | FPOVFL) & ~Alltraps;
	/* VFPv2 needs software support for underflows, so force them to zero */
	if(m->havefp == VFPv2)
		m->fpscr |= Fz;
	fpwr(Fpscr, m->fpscr);
	m->fpconfiged = 1;

	if (printed)
		return;
	sid = fprd(Fpsid);
	impl = sid >> 24;
	print("fp: %s arch %s; rev %ld\n", implement(impl),
		subarch(impl, (sid >> 16) & MASK(7)), sid & MASK(4));
	printed = 1;
}

void
fpinit(void)
{
	if (havefp()) {
		fpononly();
		fpcfg();
	}
}

void
fpon(void)
{
	if (havefp()) {
	 	fpononly();
		if (m->fpconfiged)
			fpwr(Fpscr, (fprd(Fpscr) & Allcc) | m->fpscr);
		else
			fpcfg();	/* 1st time on this fpu; configure it */
	}
}

void
fpclear(void)
{
//	ulong scr;

	fpon();
//	scr = fprd(Fpscr);
//	m->fpscr = scr & ~Allexc;
//	fpwr(Fpscr, m->fpscr);

	fpwr(Fpexc, fprd(Fpexc) & ~Fpmbc);
}


/*
 * Called when a note is about to be delivered to a
 * user process, usually at the end of a system call.
 * Note handlers are not allowed to use the FPU so
 * the state is marked (after saving if necessary) and
 * checked in the Device Not Available handler.
 */
void
fpunotify(Ureg*)
{
	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	}
	up->fpstate |= FPillegal;
}

/*
 * Called from sysnoted() via the machine-dependent
 * noted() routine.
 * Clear the flag set above in fpunotify().
 */
void
fpunoted(void)
{
	up->fpstate &= ~FPillegal;
}

/*
 * Called early in the non-interruptible path of
 * sysrfork() via the machine-dependent syscall() routine.
 * Save the state so that it can be easily copied
 * to the child process later.
 */
void
fpusysrfork(Ureg*)
{
	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	}
}

/*
 * Called later in sysrfork() via the machine-dependent
 * sysrforkchild() routine.
 * Copy the parent FPU state to the child.
 */
void
fpusysrforkchild(Proc *p, Ureg *, Proc *up)
{
	/* don't penalize the child, it hasn't done FP in a note handler. */
	p->fpstate = up->fpstate & ~FPillegal;
}

/* should only be called if p->fpstate == FPactive */
void
fpsave(FPsave *fps)
{
	int n;

	fpon();
	fps->control = fps->status = fprd(Fpscr);
	assert(m->fpnregs);
	for (n = 0; n < m->fpnregs; n++)
		fpsavereg(n, (uvlong *)fps->regs[n]);
	fpoff();
}

static void
fprestore(Proc *p)
{
	int n;

	fpon();
	fpwr(Fpscr, p->fpsave.control);
	m->fpscr = fprd(Fpscr) & ~Allcc;
	assert(m->fpnregs);
	for (n = 0; n < m->fpnregs; n++)
		fprestreg(n, *(uvlong *)p->fpsave.regs[n]);
}

/*
 * Called from sched() and sleep() via the machine-dependent
 * procsave() routine.
 * About to go in to the scheduler.
 * If the process wasn't using the FPU
 * there's nothing to do.
 */
void
fpuprocsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpoff();
		else{
			/*
			 * Fpsave() stores without handling pending
			 * unmasked exeptions. Postnote() can't be called
			 * here as sleep() already has up->rlock, so
			 * the handling of pending exceptions is delayed
			 * until the process runs again and generates an
			 * emulation fault to activate the FPU.
			 */
			fpsave(&p->fpsave);
		}
		p->fpstate = FPinactive;
	}
}

/*
 * The process has been rescheduled and is about to run.
 * Nothing to do here right now. If the process tries to use
 * the FPU again it will cause a Device Not Available
 * exception and the state will then be restored.
 */
void
fpuprocrestore(Proc *)
{
}

/*
 * Disable the FPU.
 * Called from sysexec() via sysprocsetup() to
 * set the FPU for the new process.
 */
void
fpusysprocsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
}

static void
mathnote(void)
{
	ulong status;
	char *msg, note[ERRMAX];

	status = up->fpsave.status;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	if (status & FPAINEX)
		msg = "inexact";
	else if (status & FPAOVFL)
		msg = "overflow";
	else if (status & FPAUNFL)
		msg = "underflow";
	else if (status & FPAZDIV)
		msg = "divide by zero";
	else if (status & FPAINVAL)
		msg = "bad operation";
	else
		msg = "spurious";
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=%#lux",
		msg, up->fpsave.pc, status);
	postnote(up, 1, note, NDebug);
}

static void
mathemu(Ureg *)
{
	switch(up->fpstate){
	case FPemu:
		error("illegal instruction: VFP opcode in emulated mode");
	case FPinit:
		fpinit();
		up->fpstate = FPactive;
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions.  There's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		if(up->fpsave.status & (FPAINEX|FPAUNFL|FPAOVFL|FPAZDIV|FPAINVAL)){
			mathnote();
			break;
		}
		fprestore(up);
		up->fpstate = FPactive;
		break;
	case FPactive:
		error("sys: illegal instruction: bad vfp fpu opcode");
		break;
	}
	fpclear();
}

void
fpstuck(uintptr pc)
{
	if (m->fppc == pc && m->fppid == up->pid) {
		m->fpcnt++;
		if (m->fpcnt > 4)
			panic("fpuemu: cpu%d stuck at pid %ld %s pc %#p "
				"instr %#8.8lux", m->machno, up->pid, up->text,
				pc, *(ulong *)pc);
	} else {
		m->fppid = up->pid;
		m->fppc = pc;
		m->fpcnt = 0;
	}
}

enum {
	N = 1<<31,
	Z = 1<<30,
	C = 1<<29,
	V = 1<<28,
	REGPC = 15,
};

static int
condok(int cc, int c)
{
	switch(c){
	case 0:	/* Z set */
		return cc&Z;
	case 1:	/* Z clear */
		return (cc&Z) == 0;
	case 2:	/* C set */
		return cc&C;
	case 3:	/* C clear */
		return (cc&C) == 0;
	case 4:	/* N set */
		return cc&N;
	case 5:	/* N clear */
		return (cc&N) == 0;
	case 6:	/* V set */
		return cc&V;
	case 7:	/* V clear */
		return (cc&V) == 0;
	case 8:	/* C set and Z clear */
		return cc&C && (cc&Z) == 0;
	case 9:	/* C clear or Z set */
		return (cc&C) == 0 || cc&Z;
	case 10:	/* N set and V set, or N clear and V clear */
		return (~cc&(N|V))==0 || (cc&(N|V)) == 0;
	case 11:	/* N set and V clear, or N clear and V set */
		return (cc&(N|V))==N || (cc&(N|V))==V;
	case 12:	/* Z clear, and either N set and V set or N clear and V clear */
		return (cc&Z) == 0 && ((~cc&(N|V))==0 || (cc&(N|V))==0);
	case 13:	/* Z set, or N set and V clear or N clear and V set */
		return (cc&Z) || (cc&(N|V))==N || (cc&(N|V))==V;
	case 14:	/* always */
		return 1;
	case 15:	/* never (reserved) */
		return 0;
	}
	return 0;	/* not reached */
}

/* only called to deal with user-mode instruction faults */
int
fpuemu(Ureg* ureg)
{
	int s, nfp, cop, op;
	uintptr pc;
	static int already;

	if(waserror()){
		postnote(up, 1, up->errstr, NDebug);
		return 1;
	}

	if(up->fpstate & FPillegal)
		error("floating point in note handler");

	nfp = 0;
	pc = ureg->pc;
	validaddr(pc, 4, 0);
	op  = (*(ulong *)pc >> 24) & MASK(4);
	cop = (*(ulong *)pc >>  8) & MASK(4);
	if(m->fpon)
		fpstuck(pc);		/* debugging; could move down 1 line */
	if (ISFPAOP(cop, op)) {		/* old arm 7500 fpa opcode? */
		s = spllo();
		if(!already++)
			pprint("warning: emulated arm7500 fpa instr %#8.8lux at %#p\n", *(ulong *)pc, pc);
		if(waserror()){
			splx(s);
			nexterror();
		}
		nfp = fpiarm(ureg);	/* advances pc past emulated instr(s) */
		if (nfp > 1)		/* could adjust this threshold */
			m->fppc = m->fpcnt = 0;
		splx(s);
		poperror();
	} else if (ISVFPOP(cop, op)) {	/* if vfp, fpu off or unsupported instruction */
		mathemu(ureg);		/* enable fpu & retry */
		nfp = 1;
	}

	poperror();
	return nfp;
}
{
	/* Fpexc bits */
	Fpex =		1u << 31,
	Fpenabled =	1 << 30,
	Fpdexwords                                                                                                  664       0       0         7400 12102006261  10262                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    raspberry pi

broadcom 2835 SoC (based on 2708)
arm1176jzf-s (v6 arch) 700MHz cpu, apparently dual-issue, with vfp2
videocore 4 gpu

l1 I & D VIPT caches
	16K each: 4-way, 128 sets, 32-byte lines
	l1 D is write-through, l1 I is write-back
unified l2 PIPT cache 128K: 4-way?, 1024? sets, 32-byte lines, mostly for gpu
(by default CPU doesn't see it)

we arrange that device register accesses are uncached and unbuffered
(strongly ordered, in armv6/v7 terminology).

256MB or 512MB of dram at physical address 0, shared with gpu
non-16550 uart for console
	uart serial voltages are TTL (3.3v, not rs232 which is nominally 12v);
	could use usb serial (ick).
there's no real ethernet controller, so we have to use usb ether,
and the usb controller is nastier than usual.

There's a serial port (115200b/s) on P1 connector pins (GND,TXD,RXD) =
(6,8,10).  These are 3v TTL signals: use a level-shifter to convert to
RS232, or a USB-to-TTL-serial adapter.  Add the line "console=0
b115200" to the /cfg/pxe file on the server, or the parameter
"console='0 b115200'" to cmdline.txt on the SD card.

9pi is a Plan 9 terminal, which can boot with local fossil root on the
sd card (/dev/sdM0), or with root from a Plan 9 file server via tcp.

9picpu is a Plan 9 cpu server, which could be used in a headless
configuration without screen, keyboard or mouse.

9pifat is a minimal configuration which boots a shell script boot.rc
with root in /plan9 on the dos partition, maybe useful for embedded
applications where a full Plan 9 system is not needed.

Network booting with u-boot:
start with a normal rpi u-boot sd (e.g. raspberry-pi-uboot-20120707).
update the start.elf with a version from a newer rpi distro (see below).
mk installall
add new system to ndb
see booting(8)

Booting from sd card:
- start with a normal rpi distro sd (e.g. 2012-08-16-wheezy-raspbian)
  [NB: versions of start.elf earlier than this may not be compatible]
- copy 9pi to sd's root directory
- add or change "kernel=" line in config.txt to "kernel=9pi"
- plan9.ini is built from the "kernel arguments" in cmdline.txt - each
  var=value entry becomes one plan9.ini line, so entries with spaces will
  need single quotes.


	physical mem map

hex addr size	what
----
0	 256MB	sdram, cached (newer models have 512MB)
00000000 64	exception vectors
00000100 7936	boot ATAGs (inc. cmdline.txt)
00002000 4K	Mach
00003000 1K	L2 page table for exception vectors
00003400 1K	videocore mailbox buffer
00003800 2K	FIQ stack
00004000 16K	L1 page table for kernel
00008000 -	default kernel load address
01000000 16K	u-boot env
20000000 16M	peripherals
20003000	system timer(s)
20007000	dma
2000B000	arm control: intr, timers 0 & 1, semas, doorbells, mboxes
20100000	power, reset, watchdog
20200000	gpio
20201000	uart0
20202000	mmc
20215040	uart1 (mini uart)
20300000	eMMC
20600000	smi
20980000	otg usb

40000000	l2 cache only
7e00b000	arm control
7e2000c0	jtag
7e201000?	pl011 usrt
7e215000	aux: uart1, spi[12]

80000000

c0000000	bypass caches

	virtual mem map (from cpu address map & mmu mappings)

hex addr size	what
----
0	 512MB	user process address space
7e000000 16M	i/o registers
80000000 <=224M	kzero, kernel ram (reserve some for GPU)
ffff0000 4K	exception vectors

Linux params at *R2 (default 0x100) are a sequence of ATAGs
  struct atag {
	u32int size;		/* size of ATAG in words, including header */
	u32int tag;		/* ATAG_CORE is first, ATAG_NONE is last */
	u32int data[size-2];
  };
00000000	ATAG_NONE
54410001	ATAG_CORE
54410002	ATAG_MEM
54410009	ATAG_CMDLINE

uart dmas	15, 14

intrs (96)
irq1
0	timer0
1	timer1
2	timer2
3	timer3
8	isp
9	usb
16	dma0
17	dma1
⋯
28	dma12
29	aux: uart1
30	arm
31	vpu dma

irq2
35	sdc
36	dsio
40	hdmi0
41	hdmi1
48	smi
56	sdio
57	uart1 aka "vc uart"

irq0
64	timer
65	mbox
66	doorbell0
67	doorbell1
75	usb
77	dma2
78	dma3
82	sdio
83	uart0
 * to the child process later.
 */
void
fpusysrfork(Ureg*)
{
	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	}
}

/*
 * Called later in sysrfork() via the machine-dependent
 * sysrforkchild() routine.
 * Copy the parent FPwords.pi2                                                                                              664       0       0          371 12505230743  10746                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    raspberry pi 2 -- differences from raspberry pi

broadcom 2836 SoC (based on 2709)
4 x cortex-a7 (v7 arch) 900Mhz cpu, dual-issue, vfpv3+

integral l2 cache

1GB of dram at physical address 0

peripherals   at 0x3F000000
gpu/dma space at 0xC0000000
                                                                                                                                                                                                                                                                       words.pi4                                                                                              664       0       0          720 13530464031  10744                                                                                                       ustar 00miller                          sys                                                                                                                                                                                                                    raspberry pi 4 -- work in progress!

broadcom 2838 SoC (based on 2711)
4 x cortex-a72 (1500Mhz, out-of-order pipeline)

SDCard and ethernet now supported, but USB3 still missing:
therefore probably only useful configured as a cpu server.

Needs firmware from 5 July 2019 or later.

config.txt for pi4 should include 'core_freq=250' for
the mini-uart console, and 'device_tree=' to ensure that
the loader passes an ATAG list to the kernel instead of
a device tree.
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                