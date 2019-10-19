#include <device/dma.h>
#include <plibc/stdio.h>
#include <kernel/rpi-interrupts.h>
#include <mem/kernel_alloc.h>
#include <kernel/systimer.h>

extern int dma_src_page_1;
extern int dma_dest_page_1;
extern int dma_dest_page_2;
extern int dma_cb_page;

#define CB_ALIGN 32
#define CB_ALIGN_MASK (CB_ALIGN - 1)

extern void PUT32(uint32_t addr, uint32_t value);

// WHEN L2 cache is enabled
// #define PHYS_TO_DMA(x) (x | 0X40000000)
// #define DMA_TO_PHYS(x) (x & ~(0X40000000))

// See: BCM2835-ARM-Peripherals.pdf

// BCM2836_VCBUS_0_ALIAS = $00000000;	0 Alias - L1 and L2 cached
// BCM2836_VCBUS_4_ALIAS = $40000000;	4 Alias - L2 cache coherent (non allocating)
// BCM2836_VCBUS_8_ALIAS = $80000000;	8 Alias - L2 cached (only)
// BCM2836_VCBUS_C_ALIAS = $C0000000; {C Alias - Direct uncached}	Suitable for RPi 2 Model B
// pass integer
#define PMEM_TO_DMA(x) (x | 0XC0000000)
#define DMA_TO_PMEM(x) (x & ~(0XC0000000))

// BCM2836 peripherals BCM2836_PERIPHERALS_*
// See: BCM2835-ARM-Peripherals.pdf

// BCM2836_PERIPHERALS_BASE = $3F000000;	Mapped to VC address 7E000000
// BCM2836_PERIPHERALS_SIZE = SIZE_16M;
// Pass integer
// Arm phys address to DMA address (also called bus address)
#define PIO_TO_DMA(x) (x - 0x3F000000 + 0X7E000000)

// DMA addr to arm phys address
#define DMA_TO_PIO(x) (x - 0X7E000000 + 0x3F000000)

// This will convert phys_io addr to bus io address
// 0x3F200000 - 0x3F000000 + 0X7E000000 = 0X7E200000

// We will only use 13 dma channels
static uint32_t dma_ints[13] = {
    INTERRUPT_DMA0,
    INTERRUPT_DMA1,
    INTERRUPT_DMA2,
    INTERRUPT_DMA3,
    INTERRUPT_DMA4,
    INTERRUPT_DMA5,
    INTERRUPT_DMA6,
    INTERRUPT_DMA7,
    INTERRUPT_DMA8,
    INTERRUPT_DMA9,
    INTERRUPT_DMA10,
    INTERRUPT_DMA11,
    INTERRUPT_DMA12};

static uint32_t dma_in_progress[13] = {0};

static uint32_t dma_initialized = 0;

static void dma_irq_clearer(uint32_t chan)
{
    uint32_t *dmaBaseMem = (void *)DMA_BASE;
    volatile struct DmaChannelHeader *dmaHeader = (volatile struct DmaChannelHeader *)(dmaBaseMem + (DMACH(chan)) / 4);
    dmaHeader->CS = BCM2835_DMA_INT;
    dma_in_progress[chan] = 0;
    // printf("\nDMA irq clearer called. Should be called before handler \n");
}

static void dma_0_irq_clearer(void) { dma_irq_clearer(0); }
static void dma_1_irq_clearer(void) { dma_irq_clearer(1); }
static void dma_2_irq_clearer(void) { dma_irq_clearer(2); }
static void dma_3_irq_clearer(void) { dma_irq_clearer(3); }
static void dma_4_irq_clearer(void) { dma_irq_clearer(4); }
static void dma_5_irq_clearer(void) { dma_irq_clearer(5); }
static void dma_6_irq_clearer(void) { dma_irq_clearer(6); }
static void dma_7_irq_clearer(void) { dma_irq_clearer(7); }
static void dma_8_irq_clearer(void) { dma_irq_clearer(8); }
static void dma_9_irq_clearer(void) { dma_irq_clearer(9); }
static void dma_10_irq_clearer(void) { dma_irq_clearer(10); }
static void dma_11_irq_clearer(void) { dma_irq_clearer(11); }
static void dma_12_irq_clearer(void) { dma_irq_clearer(12); }

typedef void (*clearer_callback_t)();

static clearer_callback_t dma_clearer[13] = {
    dma_0_irq_clearer,
    dma_1_irq_clearer,
    dma_2_irq_clearer,
    dma_3_irq_clearer,
    dma_4_irq_clearer,
    dma_5_irq_clearer,
    dma_6_irq_clearer,
    dma_7_irq_clearer,
    dma_8_irq_clearer,
    dma_9_irq_clearer,
    dma_10_irq_clearer,
    dma_11_irq_clearer,
    dma_12_irq_clearer};

static void dma_irq_handler(void)
{
    // printf("\nDMA irq handler called. This should be called after clearer. \n");
}

static void dma_init()
{
    if (!dma_initialized)
    {
        for (uint32_t i = 0; i < 13; i++)
        {
            register_irq_handler(dma_ints[i], dma_irq_handler, dma_clearer[i]);
        }
        dma_initialized = 1;
    }
}

void print_area(char *src_addr, int length)
{
    uart_puts("\n ----------------- \n");
    for (int i = 0; i < length; i++)
    {
        uart_putc(*src_addr++);
    }
    uart_puts("\n ----------------- \n");
}

void copy(char *src_addr, char *dst_addr, int length)
{
    while (length > 0)
    {
        *dst_addr = *src_addr;
        src_addr++;
        dst_addr++;
        length--;
    }
}

void writeBitmasked(volatile uint32_t *dest, uint32_t mask, uint32_t value)
{
    uint32_t cur = *dest;
    uint32_t new = (cur & (~mask)) | (value & mask);
    *dest = new;
    *dest = new; //added safety for when crossing memory barriers.
}

void show_dma_demo()
{

    dma_init();
    char data_array[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'Z', 'I', 'J', 'K', '\0'};

    int data_length = sizeof(data_array) / sizeof(char);

    uart_puts("Before setting source address \n");
    char *src_ptr = (char *)&dma_src_page_1;

    print_area((char *)src_ptr, data_length);

    copy(&data_array[0], (char *)&dma_src_page_1, data_length);

    uart_puts("After setting source address \n");
    print_area(src_ptr, data_length);

    uart_puts("Before copying destination 1 lookslike \n");
    print_area((char *)&dma_dest_page_1, data_length);
    uart_puts("Before copying destination 2 lookslike \n");
    print_area((char *)&dma_dest_page_2, data_length);

    uint32_t dma_chan_num = 4;
    dma_start(dma_chan_num, 0, MEM_TO_MEM, &dma_src_page_1, &dma_dest_page_1, 65536);
    dma_wait(dma_chan_num);

    uart_puts("After DMA ops \n");
    uart_puts("After DMA destination 1 lookslike \n");
    print_area((char *)&dma_dest_page_1, data_length);

    dma_chan_num = 5;
    dma_start(dma_chan_num, 0, MEM_TO_MEM, &dma_dest_page_1, &dma_dest_page_2, 65536);
    dma_wait(dma_chan_num);

    uart_puts("After DMA ops \n");
    uart_puts("After DMA destination 2 lookslike \n");
    print_area((char *)&dma_dest_page_2, data_length);
    // uart_puts("After DMA destination lookslike \n");
    // print_area((char *)&dma_dest_page_2, data_length);
}

int dma_start(int chan, int dev, DMA_DIR dir, void *src, void *dst, int len)
{
    dma_init();
    if (dma_in_progress[chan])
    {
        printf("Channel is busy. %d \n", dev);
        return -1;
    }

    uint32_t src_addr = 0;
    uint32_t dest_addr = 0;

    switch (dir)
    {
    case MEM_TO_DEV:
        src_addr = (uint32_t)PMEM_TO_DMA((uint32_t)src);
        dest_addr = (uint32_t)PIO_TO_DMA((uint32_t)dst);
        break;
    case DEV_TO_MEM:
        src_addr = (uint32_t)PIO_TO_DMA((uint32_t)src);
        dest_addr = (uint32_t)PMEM_TO_DMA((uint32_t)dst);
        break;
    case MEM_TO_MEM:
        src_addr = (uint32_t)PMEM_TO_DMA((uint32_t)src);
        dest_addr = (uint32_t)PMEM_TO_DMA((uint32_t)dst);
        break;
    default:
        break;
    }

    // Control Block's needs 32 Byte alignment (256 Bits as mentioned in Manual)
    // So we allocate 64 Bytes because even if I allocate memory at 32 Bytes alignment
    // memory allocator header info like size, and magic comes first which will break the alignment.
    void *p = mem_allocate(2 * CB_ALIGN);
    struct bcm2835_dma_cb *cb1 = (struct bcm2835_dma_cb *)(((uint32_t)p + CB_ALIGN) & ~CB_ALIGN_MASK);

    // printf(" orginal addrs: %x  modified :%x limit %x \n", p, cb1, (uint32_t)p + 32);
    cb1->info = BCM2835_DMA_S_INC | BCM2835_DMA_D_INC | BCM2835_DMA_INT_EN | dev<<16;
    cb1->src = src_addr;
    cb1->dst = dest_addr;
    cb1->length = len;
    cb1->stride = 0x0;
    cb1->next = 0x0; // last Control block

    uint32_t *dmaBaseMem = (void *)DMA_BASE;
    writeBitmasked(dmaBaseMem + BCM2835_DMA_ENABLE / 4, 1 << chan, 1 << chan);
    volatile struct DmaChannelHeader *dmaHeader = (volatile struct DmaChannelHeader *)(dmaBaseMem + (DMACH(chan)) / 4); //dmaBaseMem is a uint32_t ptr, so divide by 4 before adding byte offset
    dmaHeader->CS = BCM2835_DMA_RESET;

    MicroDelay(2);

    dmaHeader->DEBUG = BCM2835_DMA_DEBUG_READ_ERR | BCM2835_DMA_DEBUG_FIFO_ERR | BCM2835_DMA_DEBUG_LAST_NOT_SET_ERR; // clear debug error flags
    dmaHeader->CONBLK_AD = (uint32_t)PMEM_TO_DMA((uint32_t)cb1);                                                     //we have to point it to the PHYSICAL address of the control block (cb1)
    dmaHeader->CS = BCM2835_DMA_ACTIVE;                                                                              //set active bit, but everything else is 0.

    MicroDelay(2);
    mem_deallocate(p);
    return 0;
}

int dma_wait(int chan)
{
    while (dma_in_progress[chan] == 1){}
    uint32_t *dmaBaseMem = (void *)DMA_BASE;
    volatile struct DmaChannelHeader *dmaHeader = (volatile struct DmaChannelHeader *)(dmaBaseMem + (DMACH(chan)) / 4); //dmaBaseMem is a uint32_t ptr, so divide by 4 before adding byte offset
    uint32_t error = (dmaHeader->CS) & BCM2835_DMA_ERR;
    // if(error) {
        printf("DMA OPS: Channel status: %x error: %x \n", dmaHeader->CS, error);
    // }

    return 0;
}
