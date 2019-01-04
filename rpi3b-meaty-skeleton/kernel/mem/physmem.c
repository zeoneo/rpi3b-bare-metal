#include <plibc/stdio.h>
#include <stdint.h>
#include <mem/physmem.h>
#include <kernel/rpi-mailbox-interface.h>
#include <kernel/list.h>

static uint32_t arm_mem_size = 0;

extern uint8_t __kernel_end;
extern void bzero(void *, uint32_t);

static uint32_t num_pages;

DEFINE_LIST(page);
IMPLEMENT_LIST(page);

static page_t *all_pages_array;
page_list_t free_pages;

uint32_t get_num_of_free_pages()
{
    return size_page_list(&free_pages);
}

void mem_init()
{
    uint32_t mem_size, page_array_len, kernel_pages, i;

    // Get the total number of pages
    mem_size = get_mem_size();
    printf("Total Mem Size: %d Bytes \n", mem_size);
    num_pages = mem_size / PAGE_SIZE;
    printf("Total Available (4K) Page Frames: %d \n", num_pages);

    // Allocate space for all those pages' metadata.  Start this block just after the kernel image is finished
    page_array_len = sizeof(page_t) * num_pages;
    all_pages_array = (page_t *)&__kernel_end;
    bzero(all_pages_array, page_array_len);
    INITIALIZE_LIST(free_pages);

    // Iterate over all pages and mark them with the appropriate flags
    // Start with kernel pages
    kernel_pages = ((uint32_t)&__kernel_end) / PAGE_SIZE;
    for (i = 0; i < kernel_pages; i++)
    {
        all_pages_array[i].vaddr_mapped = i * PAGE_SIZE; // Identity map the kernel pages
        all_pages_array[i].flags.allocated = 1;
        all_pages_array[i].flags.kernel_page = 1;
    }
    // Map the rest of the pages as unallocated, and add them to the free list
    for (; i < num_pages; i++)
    {
        all_pages_array[i].flags.allocated = 0;
        append_page_list(&free_pages, &all_pages_array[i]);
    }
}

void *alloc_page(void)
{
    page_t *page;
    void *page_mem;

    if (size_page_list(&free_pages) == 0)
        return 0;

    // Get a free page
    page = pop_page_list(&free_pages);
    page->flags.kernel_page = 1;
    page->flags.allocated = 1;

    // Get the address the physical page metadata refers to
    page_mem = (void *)((page - all_pages_array) * PAGE_SIZE);

    // Zero out the page, big security flaw to not do this :)
    bzero(page_mem, PAGE_SIZE);

    return page_mem;
}

void free_page(void *ptr)
{
    page_t *page;

    // Get page metadata from the physical address
    page = all_pages_array + ((uint32_t)ptr / PAGE_SIZE);

    // Mark the page as free
    page->flags.allocated = 0;
    append_page_list(&free_pages, page);
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