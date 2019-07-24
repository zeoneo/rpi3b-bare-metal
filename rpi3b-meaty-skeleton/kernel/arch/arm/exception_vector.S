.section ".text.exception"
.globl __exception_table
__exception_table:
    ldr pc, _reset_h
    ldr pc, _undefined_instruction_vector_h
    ldr pc, _software_interrupt_vector_h
    ldr pc, _prefetch_abort_vector_h
    ldr pc, _data_abort_vector_h
    ldr pc, _unused_handler_h
    ldr pc, _interrupt_vector_h
    ldr pc, _fast_interrupt_vector_h

	_reset_h:                           .word   undefined_instruction_vector
	_undefined_instruction_vector_h:    .word   undefined_instruction_vector
	_software_interrupt_vector_h:       .word   software_interrupt_vector
	_prefetch_abort_vector_h:           .word   prefetch_abort_vector
	_data_abort_vector_h:               .word   data_abort_vector_asm
	_unused_handler_h:                  .word   undefined_instruction_vector
	_interrupt_vector_h:                .word   irq_handler_asm_wrapper
	_fast_interrupt_vector_h:           .word   fast_interrupt_vector


irq_handler_asm_wrapper:
    sub     lr, lr, #4
    srsdb   sp!, #0x13
    cpsid   if, #0x13
    push    {r0-r3, r12, lr}
    and     r1, sp, #4
    sub     sp, sp, r1
    push    {r1}
    bl      irq_handler
    pop     {r1}
    add     sp, sp, r1
    pop     {r0-r3, r12, lr}
    rfeia   sp!

data_abort_vector_asm:
    mov r6,lr
    ldr r8,[r6,#-8]
    mrc p15,0,r4,c5,c0,0 ;@ data/combined
    mrc p15,0,r5,c5,c0,1 ;@ instruction
    ldr sp, =__data_abort_stack_base ;@ space allocated in linker.ld
    bl hexstrings
    mov r0,r4
    bl hexstrings
    mov r0,r5
    bl hexstrings
    mov r0,r6
    bl hexstrings
    mov r0,r8
    bl hexstrings
    mov r0,r7
    bl hexstrings
123:
    b 123b
