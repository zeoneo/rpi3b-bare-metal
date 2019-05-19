#include <graphics/v3d.h>
#include <kernel/rpi-mailbox-interface.h>
#include <plibc/stdio.h>

#define v3d1 ((volatile __attribute__((aligned(4))) uint32_t *)(uintptr_t)(V3D_BASE))

typedef enum {
	CLK_EMMC_ID = 0x1,									// Mailbox Tag Channel EMMC clock ID 
	CLK_UART_ID = 0x2,									// Mailbox Tag Channel uart clock ID
	CLK_ARM_ID = 0x3,									// Mailbox Tag Channel ARM clock ID
	CLK_CORE_ID = 0x4,									// Mailbox Tag Channel SOC core clock ID
	CLK_V3D_ID = 0x5,									// Mailbox Tag Channel V3D clock ID
	CLK_H264_ID = 0x6,									// Mailbox Tag Channel H264 clock ID
	CLK_ISP_ID = 0x7,									// Mailbox Tag Channel ISP clock ID
	CLK_SDRAM_ID = 0x8,									// Mailbox Tag Channel SDRAM clock ID
	CLK_PIXEL_ID = 0x9,									// Mailbox Tag Channel PIXEL clock ID
	CLK_PWM_ID = 0xA,									// Mailbox Tag Channel PWM clock ID
} MB_CLOCK_ID;

// https://github.com/raspberrypi/documentation/blob/JamesH65-mailbox_docs/configuration/mailboxes/propertiesARM-VC.md

void set_max_speed() {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_GET_MAX_CLOCK_RATE, CLK_V3D_ID);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_GET_MAX_CLOCK_RATE);
    uint32_t max_clock_rate = mp->data.buffer_32[1];

    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_CLOCK_RATE, CLK_V3D_ID, max_clock_rate, 1);
    RPI_PropertyProcess();
    mp = RPI_PropertyGet(TAG_SET_CLOCK_RATE);

    printf("\n clock_id: %d", (uint32_t)(mp->data.buffer_32[0]));
    printf("\n max_clock_rate: %d \n", max_clock_rate);

}

bool init_v3d (void) {
    set_max_speed();
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_ENABLE_QPU, !0);
    RPI_PropertyProcess();
    printf("\n v3d1[V3D_IDENT0]:%x \n", v3d1[V3D_IDENT0]);
	if (v3d1[V3D_IDENT0] == 0x02443356) { // Magic number.
    	return true;
	}
	return false;
}

uint32_t v3d_mem_alloc (uint32_t size, uint32_t align, uint32_t flags) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_ALLOCATE_MEMORY, size, align, flags);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_ALLOCATE_MEMORY);
    return mp->data.value_32;
}

bool v3d_mem_free (uint32_t handle) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_RELEASE_MEMORY, handle);
    RPI_PropertyProcess();
    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_RELEASE_MEMORY);
    return mp->data.value_32 == 0;
}

uint32_t v3d_mem_lock (uint32_t handle) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_LOCK_MEMORY, handle);
    RPI_PropertyProcess();
    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_LOCK_MEMORY);
    return mp->data.value_32;
}

bool v3d_mem_unlock (uint32_t handle) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_UNLOCK_MEMORY, handle);
    RPI_PropertyProcess();
    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_UNLOCK_MEMORY);
    return mp->data.value_32 == 0;
}

bool v3d_execute_code (uint32_t code, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_EXECUTE_CODE, code, r0, r1, r2, r3, r4, r5);
    RPI_PropertyProcess();
    // rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_EXECUTE_CODE);
    return true;
}

bool v3d_execute_qpu (int32_t num_qpus, uint32_t control, uint32_t noflush, uint32_t timeout) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_EXECUTE_QPU, num_qpus, control, noflush, timeout);
    RPI_PropertyProcess();
    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_UNLOCK_MEMORY);
    return mp->data.value_32 == 0;
}