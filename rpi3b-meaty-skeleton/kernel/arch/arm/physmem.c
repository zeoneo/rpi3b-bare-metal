#include <kernel/physmem.h>
#include <kernel/rpi-mailbox-interface.h>

uint32_t get_mem_size()
{
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_GET_ARM_MEMORY);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp;
    mp = RPI_PropertyGet(TAG_GET_ARM_MEMORY);

    if (mp)
    {
        return (int)(mp->data.buffer_32[1]);
    }
    return -1;
}