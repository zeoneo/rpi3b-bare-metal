#include <device/dma.h>
#include <device/uart0.h>
#include <klib/printk.h>
#include <kernel/rpi-interrupts.h>

int dma_src_page_1 = 0x0;
int dma_dest_page_1 = 0x0;
int dma_dest_page_2 = 0x0;
int dma_cb_page = 0x0; //TODO: Fix this

extern void PUT32(uint32_t addr, uint32_t value);

int dmaChNum = 4;

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

static void dma_0_irq_handler(void)
{
    printk("\nDMA irq handler called. This should be called after clearer. \n");
}

static void dma_0_irq_clearer(void)
{
    uint32_t *dmaBaseMem = (void *)DMA_BASE;
    volatile struct DmaChannelHeader *dmaHeader = (volatile struct DmaChannelHeader *)(dmaBaseMem + (DMACH(dmaChNum)) / 4);
    dmaHeader->CS = BCM2835_DMA_INT;
    printk("\nDMA irq clearer called. Should be called before handler \n");
}

void show_dma_demo()
{

    register_irq_handler(INTERRUPT_DMA4, dma_0_irq_handler, dma_0_irq_clearer);

    char data_array[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'Z', 'I', 'J', 'K', '\0'};

    int data_length = sizeof(data_array) / sizeof(char);

    uart_puts("Before setting source address \n");
    char *src_ptr = (char *)&dma_src_page_1;

    print_area((char *)src_ptr, data_length);

    copy(&data_array[0], (char *)&dma_src_page_1, data_length);

    uart_puts("After setting source address \n");
    print_area(src_ptr, data_length);

    uart_puts("Before copying destination lookslike \n");
    print_area((char *)&dma_dest_page_1, data_length);

    uart_puts("Before copying destination lookslike \n");
    print_area((char *)&dma_dest_page_2, data_length);

    struct bcm2835_dma_cb *cb1 = (struct bcm2835_dma_cb *)&dma_cb_page;

    struct bcm2835_dma_cb *cb2 = cb1 + 1; /// 32 Byte aligned control block address

    cb1->info = BCM2835_DMA_S_INC | BCM2835_DMA_D_INC;
    cb1->src = (uint32_t)&dma_src_page_1;
    cb1->dst = (uint32_t)&dma_dest_page_1;
    cb1->length = data_length;
    cb1->stride = 0x0;
    cb1->next = (uint32_t)cb2;

    cb2->info = BCM2835_DMA_S_INC | BCM2835_DMA_D_INC | BCM2835_DMA_INT_EN;
    cb2->src = (uint32_t)&dma_src_page_1;
    cb2->dst = (uint32_t)&dma_dest_page_2;
    cb2->length = data_length;
    cb2->stride = 0x0;
    cb2->next = 0x0; // last Control block

    printk("\n cb1: %x", cb1);
    printk("\n cb2: %x", cb2);

    uint32_t *dmaBaseMem = (void *)DMA_BASE;

    writeBitmasked(dmaBaseMem + BCM2835_DMA_ENABLE / 4, 1 << dmaChNum, 1 << dmaChNum);

    //configure the DMA header to point to our control block:
    volatile struct DmaChannelHeader *dmaHeader = (volatile struct DmaChannelHeader *)(dmaBaseMem + (DMACH(dmaChNum)) / 4); //dmaBaseMem is a uint32_t ptr, so divide by 4 before adding byte offset
    dmaHeader->CS = BCM2835_DMA_RESET;                                                                                      //make sure to disable dma first.
                                                                                                                            //give time for the reset command to be handled.

    unsigned int a1 = 55000;
    while (a1 > 0)
    {
        a1--;
    }
    uart_puts("After DMA reset \n");

    dmaHeader->DEBUG = BCM2835_DMA_DEBUG_READ_ERR | BCM2835_DMA_DEBUG_FIFO_ERR | BCM2835_DMA_DEBUG_LAST_NOT_SET_ERR; // clear debug error flags
    uart_puts("After DMA debug \n");
    dmaHeader->CONBLK_AD = (uint32_t)&dma_cb_page; //we have to point it to the PHYSICAL address of the control block (cb1)
    uart_puts("After DMA address \n");
    dmaHeader->CS = BCM2835_DMA_ACTIVE; //set active bit, but everything else is 0.
    uart_puts("After DMA activate \n");

    unsigned int a = 55000;
    while (a > 0)
    {
        a--;
    }
    uart_puts("After DMA ops \n");
    uart_puts("After DMA destination lookslike \n");
    print_area((char *)&dma_dest_page_1, data_length);

    uart_puts("After DMA destination lookslike \n");
    print_area((char *)&dma_dest_page_2, data_length);
}
