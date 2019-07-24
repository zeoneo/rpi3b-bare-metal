#include <klib/printk.h>
#include <stdint.h>
#include <mem/physmem.h>
#include <kernel/rpi-mailbox-interface.h>
#include <kernel/list.h>

static uint32_t arm_mem_size = 0;

extern uint8_t __kernel_end;
extern uint8_t __second_lvl_tbl_end;
extern uint8_t __virt_start;
extern uint8_t __text_boot_end_aligned;

extern void bzero(void *, uint32_t);

static uint32_t num_pages;

static uint32_t num_of_free_pages;
static uint32_t first_page_index;

static page_t *all_pages_array;

uint32_t get_num_of_free_pages()
{
    return num_of_free_pages;
}

void mem_init()
{
    uint32_t mem_size;
    // Get the total number of pages
    mem_size = get_mem_size();
    num_pages = mem_size / PAGE_SIZE;

    // Allocate space for all those pages' metadata.  Start this block just after the kernel image is finished
    uint32_t page_array_len =  num_pages * sizeof(page_t);
    all_pages_array = (page_t *)&__second_lvl_tbl_end; // Here we have reserved the memory 256 KB for mem manager DS.

    uint32_t x= 0;
    uint32_t *temp_ptr = (uint32_t *)all_pages_array;
    while(x <= page_array_len) {
        *(temp_ptr + x) = 0;
        x++;
    }

    uint32_t kernel_pages, i;
    // Iterate over all pages and mark them with the appropriate flags
    // Start with kernel pages
    kernel_pages = (((uint32_t)&__kernel_end) - ((uint32_t)&__virt_start)) / PAGE_SIZE;
    kernel_pages += ((uint32_t)&__text_boot_end_aligned) / PAGE_SIZE; //boot mapping
    for (i = 0; i < kernel_pages; i++)
    {
        uint32_t vaddr = i * PAGE_SIZE;
        all_pages_array[i].vaddr_higher = (uint16_t)(vaddr & 0xffff0000); // Identity map the kernel pages
        all_pages_array[i].vaddr_lower = (uint8_t)(vaddr & 0xff00); // Identity map the kernel pages
        all_pages_array[i].flags.allocated = 1;
        all_pages_array[i].flags.kernel_page = 1;
    }

    num_of_free_pages = 0;
    first_page_index = i;
    //Map the rest of the pages as unallocated, and add them to the free list
    for (; i < num_pages; i++)
    {
        all_pages_array[i].flags.allocated = 0;
        num_of_free_pages++;
    }
}

void *alloc_page(void)
{
    page_t *page;

    if (num_of_free_pages == 0) {
        return 0;
    }

    uint32_t free_page_index;
    for(free_page_index = first_page_index; free_page_index < num_pages; free_page_index++) {
        if(all_pages_array[free_page_index].flags.allocated == 0) {
            first_page_index = free_page_index + 1;
            break;
        }
    }

    // Get a free page
    page = &all_pages_array[free_page_index];
    page->flags.kernel_page = 1;
    page->flags.allocated = 1;
    num_of_free_pages--;
    // Get the address the physical page metadata refers to
    return (void *)(free_page_index * PAGE_SIZE);
}

void free_page(void *ptr)
{
    page_t *page;

    // Get page metadata from the physical address
    uint32_t index = ((uint32_t)ptr / PAGE_SIZE);
    page = &all_pages_array[index];

    // Mark the page as free
    page->flags.allocated = 0;
    first_page_index = index;
    num_of_free_pages++;
}

uint32_t get_mem_size()
{
    if (arm_mem_size != 0)
    {
        return arm_mem_size;
    }

    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_GET_ARM_MEMORY);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp;
    mp = RPI_PropertyGet(TAG_GET_ARM_MEMORY);

    if (mp)
    {
        arm_mem_size = (int32_t)(mp->data.buffer_32[1]);
        return arm_mem_size;
    }
    return -1;
}