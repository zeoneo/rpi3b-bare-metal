// To keep this in the first portion of the binary.
.section ".text.boot"
 
// Make _start global.
.globl _start
.global _get_stack_pointer
.global _exception_table
.global _enable_interrupts

.equ ARM_MODE_USR, 0x10;								;@ CPU in USR mode .. Normal User mode
.equ ARM_MODE_FIQ, 0x11;								;@ CPU in FIQ mode .. FIQ Processing
.equ ARM_MODE_IRQ, 0x12;								;@ CPU in IRQ mode .. IRQ Processing
.equ ARM_MODE_SVC, 0x13;								;@ CPU in SVC mode .. Service mode
.equ ARM_MODE_HYP, 0x1A;								;@ CPU in HYP mode .. Hypervisor mode  (ARM7/ARM8 only)
.equ ARM_MODE_UND, 0x1B;								;@ CPU in UND mode .. Undefined Instructions mode
.equ ARM_MODE_SYS, 0x1F;	

.equ ARM_MODE_MASK, 0x1F;								;@ Mask to clear all but CPU mode bits from cpsr register
.equ ARM_I_BIT,		0x80;								;@ IRQs disabled when set to 1
.equ ARM_F_BIT,		0x40;	

.balign 4

_start:

	mov r12, pc											;@ Hold boot address in high register R12
	mrs r0, CPSR										;@ Fetch the cpsr register
	orr r0, r0, #(ARM_I_BIT | ARM_F_BIT)				;@ Disable Irq/Fiq
	and r11, r0, #ARM_MODE_MASK							;@ Clear all but CPU mode bits in register r11

 	cmp r11, #ARM_MODE_HYP								;@ Check we are in HYP_MODE											
	bne .NotInHypMode									;@ Branch if not equal meaning was not in HYP_MODE  
	bic r0, r0, #ARM_MODE_MASK							;@ Clear the CPU mode bits in register r0							
	orr r0, r0, #ARM_MODE_SVC							;@ ARM_MODE_SVC bits onto register	
    msr spsr_cxsf,r0									;@ Hold value in spsr_cxsf
    add lr,pc,#4										;@ Calculate address of .NotInHypMode label

	msr ELR_hyp, lr
	eret

.NotInHypMode:
    ldr pc, _reset_h
    ldr pc, _undefined_instruction_vector_h
    ldr pc, _software_interrupt_vector_h
    ldr pc, _prefetch_abort_vector_h
    ldr pc, _data_abort_vector_h
    ldr pc, _unused_handler_h
    ldr pc, _interrupt_vector_h
    ldr pc, _fast_interrupt_vector_h

_reset_h:                           .word   _reset_
_undefined_instruction_vector_h:    .word   undefined_instruction_vector
_software_interrupt_vector_h:       .word   software_interrupt_vector
_prefetch_abort_vector_h:           .word   prefetch_abort_vector
_data_abort_vector_h:               .word   data_abort_vector
_unused_handler_h:                  .word   _reset_
_interrupt_vector_h:                .word   interrupt_vector
_fast_interrupt_vector_h:           .word   fast_interrupt_vector


_reset_:
    mov     r0, #0x8000
    mov     r1, #0x0000
    ldmia   r0!,{r2, r3, r4, r5, r6, r7, r8, r9}
    stmia   r1!,{r2, r3, r4, r5, r6, r7, r8, r9}
    ldmia   r0!,{r2, r3, r4, r5, r6, r7, r8, r9}
    stmia   r1!,{r2, r3, r4, r5, r6, r7, r8, r9}

	bic r0, r0, #ARM_MODE_MASK							;@ Clear the CPU mode bits in register r0							
	orr r0, r0, #ARM_MODE_IRQ							;@ SVC_MODE bits onto register
    msr CPSR_c, r0
    mov sp, #0x7000


	// Change to supervisor mode. Set stack pointer
	// For each mode sp holds different values.
	bic r0, r0, #ARM_MODE_MASK							;@ Clear the CPU mode bits in register r0							
	orr r0, r0, #ARM_MODE_SVC							;@ SVC_MODE bits onto register
    msr CPSR_c, r0
    mov sp, #0x8000

 
 
	// Call clear-bss.c
	bl _clear_bss

 
	// halt
_inf_loop:
    b       _inf_loop

_get_stack_pointer:
    // Return the stack pointer value
    str     sp, [sp]
    ldr     r0, [sp]

    // Return from the function
    mov     pc, lr


_enable_interrupts:
    mrs     r0, cpsr
    bic     r0, r0, #0x80
    msr     cpsr_c, r0

    mov     pc, lr
