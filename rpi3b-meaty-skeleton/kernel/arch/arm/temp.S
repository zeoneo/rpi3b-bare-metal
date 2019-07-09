.globl PUT32
PUT32:
    str r1,[r0]
    bx lr

.globl GET32
GET32:
    ldr r0,[r0]
    bx lr

.globl dummy
dummy:
    mov     pc, lr

.globl enable_irq
enable_irq:
    mrs r0,cpsr
    bic r0,r0,#0x80
    msr cpsr_c,r0
    bx lr

.globl _get_stack_pointer
_get_stack_pointer:
    // Return the stack pointer value
    str     sp, [sp]
    ldr     r0, [sp]

    // Return from the function
    mov     pc, lr

.globl _enable_interrupts
_enable_interrupts:
    mrs     r0, cpsr
    bic     r0, r0, #0x80
    msr     cpsr_c, r0
    mov     pc, lr

.globl irq_handler_asm_wrapper
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

.globl data_abort_vector_asm
data_abort_vector_asm:
    mov r6,lr
    ldr r8,[r6,#-8]
    mrc p15,0,r4,c5,c0,0 ;@ data/combined
    mrc p15,0,r5,c5,c0,1 ;@ instruction
    mov sp,#0x00004000
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


.globl read_cpu_id
read_cpu_id:
    // read cpu id, stop slave cores
    // mrc p15, 0, r0, c0, c0, 0
    mrc p15, 0, r0,c0,c0,5
    bx lr

.globl dmb
dmb:
	mov	r12, #0
	mcr	p15, 0, r12, c7, c10, 5
    bx lr

.section .text.ARMaddrToGPUaddr, "ax", %progbits
.balign	4
.globl ARMaddrToGPUaddr;		
.type ARMaddrToGPUaddr, %function
ARMaddrToGPUaddr:
	mov r1, #0xC0000000												;@ Fetch bus alias	
	orr r0, r0, r1										;@ Create bus address
	bx   lr												;@ Return
.balign	4
.ltorg													;@ Tell assembler ltorg data for this code can go here
.size	ARMaddrToGPUaddr, .-ARMaddrToGPUaddr

;@"========================================================================="
@#		GPUaddrToARMaddr -- Composite Pi1, Pi2 & Pi3 code
@#		C Function: uint32_t GPUaddrToARMaddr (uint32_t BUSaddress);
@#		Entry: R0 will have GPUAddress value
;@"========================================================================="
.section .text.GPUaddrToARMaddr, "ax", %progbits
.balign	4
.globl GPUaddrToARMaddr;		
.type GPUaddrToARMaddr, %function
GPUaddrToARMaddr:
    mov r1, #0xC0000000											;@ Fetch bus alias	
	bic r0, r0, r1										;@ Create arm address
	bx   lr												;@ Return
.balign	4
.ltorg													;@ Tell assembler ltorg data for this code can go here
.size	GPUaddrToARMaddr, .-GPUaddrToARMaddr
