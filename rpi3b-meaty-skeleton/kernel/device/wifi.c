#include<device/wifi.h>
#include<device/gpio.h>
#include<device/sdio.h>
// #include<device/emmc.h>
#include<plibc/stdio.h>
#include<stdint.h>

extern uint32_t emmccmd(uint32_t cmd, uint32_t arg, uint32_t *resp);
extern void reset_cmd_circuit();

uint32_t sdio_cmd(cmd_index_t cmd_index, uint32_t arg) {
	uint32_t resp[4] = {0};
	sdio_send_command(cmd_index, arg, &resp[0]);
	return resp[0];
}

static uint32_t sdio_old_cmd(uint32_t cmd_index, uint32_t arg) {
	uint32_t resp[4] = {0};
	emmccmd(cmd_index, arg, &resp[0]);
	return resp[0];
}

#define V3_3  1<<20

void enable_wifi(void) {
	/* disconnect emmc from SD card (connect sdhost instead) */

    uint32_t i;

	// Following lines connect to SD card to SD HOST
	for(i = 48; i <= 53; i++)
		select_alt_func(i, Alt0);


	// FOllowing lines connect EMMC controller to wifi
	for(i = 34; i <= 39; i++){
		select_alt_func(i, Alt3);
		if(i == 34)
			disable_pulling(i); // Pull off
		else
			pullup_pin(i);
	}

    SDRESULT resp = initialize_sdio();
    if(resp != SD_OK) {
        printf("Error initializing sdio interface ");
        return;
    }

	uint32_t ocr = sdio_cmd(IX_GO_IDLE_STATE, 0);
	ocr = sdio_cmd(5, 0);
	printf(" CMD5: ocr: %x \n", ocr);
	uint32_t cmd52_arg = 0b10001000000000000000010010000000;
	ocr = sdio_cmd(52, cmd52_arg);
	
	printf(" CMD52: ocr: %x \n", ocr);

	ocr = sdio_old_cmd(IX_IO_SEND_OP_COND, V3_3);
	i = 0;
	while((ocr & (1<<31)) == 0){
		if(++i > 5){
			printf("ether4330: no response to sdio access: ocr = %x\n", ocr);
			break;
		}
		ocr = sdio_old_cmd(IX_IO_SEND_OP_COND, V3_3);
	}


	reset_cmd_circuit();

	
	printf("Final OCR: %x \n",ocr);

    printf("wifi enabled");  
}


