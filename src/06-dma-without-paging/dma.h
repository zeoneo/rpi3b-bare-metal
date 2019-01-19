#ifndef DMA_H
#define DMA_H
#include "rpi-base.h"
#include <stdint.h>

// Taken all of the following code is taken from linux source
#define DMA_BASE PERIPHERAL_BASE + 0x7000

#define BIT(nr) (1UL << (nr))

struct bcm2835_dma_cb
{
    uint32_t info;
    uint32_t src;
    uint32_t dst;
    uint32_t length;
    uint32_t stride;
    uint32_t next;
    uint32_t pad[2];
};

struct bcm2835_cb_entry
{
    struct bcm2835_dma_cb *cb;
    uint32_t paddr;
};

#define DMACH(n) (0x100 * (n))

struct DmaChannelHeader
{
    uint32_t CS; //Control and Status
        //31    RESET; set to 1 to reset DMA
        //30    ABORT; set to 1 to abort current DMA control block (next one will be loaded & continue)
        //29    DISDEBUG; set to 1 and DMA won't be paused when debug signal is sent
        //28    WAIT_FOR_OUTSTANDING_WRITES; set to 1 and DMA will wait until peripheral says all writes have gone through before loading next CB
        //24-27 reserved
        //20-23 PANIC_PRIORITY; 0 is lowest priority
        //16-19 PRIORITY; bus scheduling priority. 0 is lowest
        //9-15  reserved
        //8     ERROR; read as 1 when error is encountered. error can be found in DEBUG register.
        //7     reserved
        //6     WAITING_FOR_OUTSTANDING_WRITES; read as 1 when waiting for outstanding writes
        //5     DREQ_STOPS_DMA; read as 1 if DREQ is currently preventing DMA
        //4     PAUSED; read as 1 if DMA is paused
        //3     DREQ; copy of the data request signal from the peripheral, if DREQ is enabled. reads as 1 if data is being requested, else 0
        //2     INT; set when current CB ends and its INTEN=1. Write a 1 to this register to clear it
        //1     END; set when the transfer defined by current CB is complete. Write 1 to clear.
        //0     ACTIVE; write 1 to activate DMA (load the CB before hand)
    uint32_t CONBLK_AD; //Control Block Address
    uint32_t TI;        //transfer information; see DmaControlBlock.TI for description
    uint32_t SOURCE_AD; //Source address
    uint32_t DEST_AD;   //Destination address
    uint32_t TXFR_LEN;  //transfer length.
    uint32_t STRIDE;    //2D Mode Stride. Only used if TI.TDMODE = 1
    uint32_t NEXTCONBK; //Next control block. Must be 256-bit aligned (32 bytes; 8 words)
    uint32_t DEBUG;     //controls debug settings
};

#define BCM2835_DMA_MAX_DMA_CHAN_SUPPORTED 14
#define BCM2835_DMA_CHAN_NAME_SIZE 8
#define BCM2835_DMA_BULK_MASK BIT(0)

#define BCM2835_DMA_CS 0x00
#define BCM2835_DMA_ADDR 0x04
#define BCM2835_DMA_TI 0x08
#define BCM2835_DMA_SOURCE_AD 0x0c
#define BCM2835_DMA_DEST_AD 0x10
#define BCM2835_DMA_LEN 0x14
#define BCM2835_DMA_STRIDE 0x18
#define BCM2835_DMA_NEXTCB 0x1c
#define BCM2835_DMA_DEBUG 0x20

/* DMA CS Control and Status bits */
#define BCM2835_DMA_ACTIVE BIT(0)             /* activate the DMA */
#define BCM2835_DMA_END BIT(1)                /* current CB has ended */
#define BCM2835_DMA_INT BIT(2)                /* interrupt status */
#define BCM2835_DMA_DREQ BIT(3)               /* DREQ state */
#define BCM2835_DMA_ISPAUSED BIT(4)           /* Pause requested or not active */
#define BCM2835_DMA_ISHELD BIT(5)             /* Is held by DREQ flow control */
#define BCM2835_DMA_WAITING_FOR_WRITES BIT(6) /* waiting for last \
                                               * AXI-write to ack \
                                               */
#define BCM2835_DMA_ERR BIT(8)
#define BCM2835_DMA_PRIORITY(x) ((x & 15) << 16)       /* AXI priority */
#define BCM2835_DMA_PANIC_PRIORITY(x) ((x & 15) << 20) /* panic priority */
/* current value of TI.BCM2835_DMA_WAIT_RESP */
#define BCM2835_DMA_WAIT_FOR_WRITES BIT(28)
#define BCM2835_DMA_DIS_DEBUG BIT(29) /* disable debug pause signal */
#define BCM2835_DMA_ABORT BIT(30)     /* Stop current CB, go to next, WO */
#define BCM2835_DMA_RESET BIT(31)     /* WO, self clearing */

/* Transfer information bits - also bcm2835_cb.info field */
#define BCM2835_DMA_INT_EN BIT(0)
#define BCM2835_DMA_TDMODE BIT(1)    /* 2D-Mode */
#define BCM2835_DMA_WAIT_RESP BIT(3) /* wait for AXI-write to be acked */
#define BCM2835_DMA_D_INC BIT(4)     /* Increment destination */
#define BCM2835_DMA_D_WIDTH BIT(5)   /* 128bit writes if set */
#define BCM2835_DMA_D_DREQ BIT(6)    /* enable DREQ for destination */
#define BCM2835_DMA_D_IGNORE BIT(7)  /* ignore destination writes */
#define BCM2835_DMA_S_INC BIT(8)     /* Increment Source */
#define BCM2835_DMA_S_WIDTH BIT(9)   /* 128bit writes if set */
#define BCM2835_DMA_S_DREQ BIT(10)   /* enable SREQ for source */
#define BCM2835_DMA_S_IGNORE BIT(11) /* ignore source reads - read 0 */
#define BCM2835_DMA_BURST_LENGTH(x) ((x & 15) << 12)
#define BCM2835_DMA_PER_MAP(x) ((x & 31) << 16) /* REQ source */
#define BCM2835_DMA_WAIT(x) ((x & 31) << 21)    /* add DMA-wait cycles */
#define BCM2835_DMA_NO_WIDE_BURSTS BIT(26)      /* no 2 beat write bursts */

/* debug register bits */
#define BCM2835_DMA_DEBUG_LAST_NOT_SET_ERR BIT(0)
#define BCM2835_DMA_DEBUG_FIFO_ERR BIT(1)
#define BCM2835_DMA_DEBUG_READ_ERR BIT(2)
#define BCM2835_DMA_DEBUG_OUTSTANDING_WRITES_SHIFT 4
#define BCM2835_DMA_DEBUG_OUTSTANDING_WRITES_BITS 4
#define BCM2835_DMA_DEBUG_ID_SHIFT 16
#define BCM2835_DMA_DEBUG_ID_BITS 9
#define BCM2835_DMA_DEBUG_STATE_SHIFT 16
#define BCM2835_DMA_DEBUG_STATE_BITS 9
#define BCM2835_DMA_DEBUG_VERSION_SHIFT 25
#define BCM2835_DMA_DEBUG_VERSION_BITS 3
#define BCM2835_DMA_DEBUG_LITE BIT(28)

/* shared registers for all dma channels */
#define BCM2835_DMA_INT_STATUS 0xfe0
#define BCM2835_DMA_ENABLE 0xff0

#define BCM2835_DMA_DATA_TYPE_S8 1
#define BCM2835_DMA_DATA_TYPE_S16 2
#define BCM2835_DMA_DATA_TYPE_S32 4
#define BCM2835_DMA_DATA_TYPE_S128 16

/* Valid only for channels 0 - 14, 15 has its own base address */
#define BCM2835_DMA_CHAN(n) ((n) << 8) /* Base address */
#define BCM2835_DMA_CHANIO(base, n) ((base) + BCM2835_DMA_CHAN(n))

// For peripheral
// #define TIMER_BASE 0x3F003000
// #define DMA_BASE 0x3F007000
// #define CLOCK_BASE 0x3F101000
// #define GPIO_BASE 0x3F200000
// #define PWM_BASE 0x3F20C000
// #define GPIO_BASE_BUS 0x7E200000 //this is the physical bus address of the GPIO module. This is only used when other peripherals directly connected to the bus (like DMA) need to read/write the GPIOs
// #define PWM_BASE_BUS 0x7E20C000

// This is used when other peripheral which are directly connected to bus
// SPI with DMA, or GPIO with DMA can use this macro later.
#define BUS_TO_PHYS(x) ((x) & ~0xC0000000)

#define GPIO_REGISTER_BASE 0x200000
#define GPIO_SET_OFFSET 0x1C
#define GPIO_CLR_OFFSET 0x28
#define PHYSICAL_GPIO_BUS (0x7E000000 + GPIO_REGISTER_BASE)

void show_dma_demo();
#endif