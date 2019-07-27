
#include<stdint.h>
#include<klib/printk.h>

uint32_t handle_syscall(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

void sys_write(char * buf){
	printk(buf);
}

int sys_fork(){
	return printk("In Psys fork");
}

void sys_exit(){
    printk("Exit Process");
}

void * const sys_call_table[] = {sys_write, sys_fork, sys_exit};
int (*call_sys_method)();

uint32_t handle_syscall(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    register long r7 __asm__ ("r7");
    printk(" ro: %d r1: %d r2:%d r3:%d", r0, r1, r2, r3);

    call_sys_method = sys_call_table[r7];
    call_sys_method();
    return 0;
}

