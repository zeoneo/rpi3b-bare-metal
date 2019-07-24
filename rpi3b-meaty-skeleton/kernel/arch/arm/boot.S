// To keep this in the first portion of the binary.
.section ".text.boot"
 
// Make _start global.
.globl _start
.global _get_stack_pointer
.global __code_start
.global _enable_interrupts

.equ ARM_MODE_USR, 0x10;                                ;@ CPU in USR mode .. Normal User mode
.equ ARM_MODE_FIQ, 0x11;                                ;@ CPU in FIQ mode .. FIQ Processing
.equ ARM_MODE_IRQ, 0x12;                                ;@ CPU in IRQ mode .. IRQ Processing
.equ ARM_MODE_SVC, 0x13;                                ;@ CPU in SVC mode .. Service mode
.equ ARM_MODE_HYP, 0x1A;                                ;@ CPU in HYP mode .. Hypervisor mode  (ARM7/ARM8 only)
.equ ARM_MODE_UND, 0x1B;                                ;@ CPU in UND mode .. Undefined Instructions mode
.equ ARM_MODE_SYS, 0x1F;    

.equ ARM_MODE_MASK, 0x1F;                                ;@ Mask to clear all but CPU mode bits from cpsr register
.equ ARM_I_BIT,        0x80;                                ;@ IRQs disabled when set to 1
.equ ARM_F_BIT,        0x40;

.equ    CPSR_MODE_USER,         0x10
.equ    CPSR_MODE_FIQ,          0x11
.equ    CPSR_MODE_IRQ,          0x12
.equ    CPSR_MODE_SVR,          0x13
.equ    CPSR_MODE_ABORT,        0x17
.equ    CPSR_MODE_UNDEFINED,    0x1B
.equ    CPSR_MODE_SYSTEM,       0x1F

// See ARM section A2.5 (Program status registers)
.equ    CPSR_IRQ_INHIBIT,       0x80
.equ    CPSR_FIQ_INHIBIT,       0x40
.equ    CPSR_THUMB,             0x20

.equ    SCTLR_ENABLE_DATA_CACHE,        0x4
.equ    SCTLR_ENABLE_BRANCH_PREDICTION, 0x800
.equ    SCTLR_ENABLE_INSTRUCTION_CACHE, 0x1000


.balign 4
_start:
    mrc p15, 0, r6,c0,c0,5
    and     r6, r6, #3
    mov r7, #0
    cmp     r6, r7
    beq 2f
    // cpu id > 0, stop
1:  wfe
    b       1b
2:  // cpu id == 0


    mov r12, pc                                            ;@ Hold boot address in high register R12
    mrs r0, CPSR                                        ;@ Fetch the cpsr register
    orr r0, r0, #(ARM_I_BIT | ARM_F_BIT)                ;@ Disable Irq/Fiq
    and r11, r0, #ARM_MODE_MASK                            ;@ Clear all but CPU mode bits in register r11

     cmp r11, #ARM_MODE_HYP                                ;@ Check we are in HYP_MODE                                            
    bne .NotInHypMode                                    ;@ Branch if not equal meaning was not in HYP_MODE  
    bic r0, r0, #ARM_MODE_MASK                            ;@ Clear the CPU mode bits in register r0                            
    orr r0, r0, #ARM_MODE_SVC                            ;@ ARM_MODE_SVC bits onto register    
    msr spsr_cxsf,r0                                    ;@ Hold value in spsr_cxsf
    add lr,pc,#4                                        ;@ Calculate address of .NotInHypMode label

    msr ELR_hyp, lr
    eret

.NotInHypMode:
    // enable unaligned address access
    mrc p15, 0, r0, c1, c0, 0
    orr r0, #1 << 22
    mcr p15, 0, r0, c1, c0, 0

    // Enable VFP ------------------------------------------------------------

    // r1 = Access Control Register
    MRC p15, #0, r1, c1, c0, #2
    // enable full access for p10,11
    ORR r1, r1, #(0xf << 20)
    // ccess Control Register = r1
    MCR p15, #0, r1, c1, c0, #2
    MOV r1, #0
    // flush prefetch buffer because of FMXR below
    MCR p15, #0, r1, c7, c5, #4
    // and CP 10 & 11 were only just enabled
    // Enable VFP itself
    MOV r0,#0x40000000
    // FPEXC = r0
    FMXR FPEXC, r0


    mov r0,#0xD3
    msr cpsr_c,r0
    mov sp, #0x8000 ;@ Temporarily initialize stack pointer to 0x8000 it will grow down to lower memory address
    ;@ After mapping higher half of the kernel we need to reinitialize stack pointer
    ;@ SP is initialized here bcoz I have poor asm skills and I need to call C funtion "initialize_virtual_memory" which might use stack pointer
    ldr r8, =_clear_bss

    ldr r0, =__code_start
    
    ;@ MRC p15, 0, <Rt>, c12, c0, 0 ; Read VBAR into Rt 
    MCR p15, 0, r0, c12, c0, 0 ;@ Write Rt to VBAR

    bl initialize_virtual_memory
    ;@ (PSR_IRQ_MODE|PSR_FIQ_DIS|PSR_IRQ_DIS)
    mov r0,#0xD2
    msr cpsr_c,r0
    ldr sp, =__irq_stack_base

    ;@ (PSR_SVC_MODE|PSR_FIQ_DIS|PSR_IRQ_DIS)
    mov r0,#0xD3
    msr cpsr_c,r0
    ldr sp, =__svc_stack_base



    bx r8 ;@jump clear bss

    b _inf_loop

    // halt
_inf_loop:
    b       _inf_loop

.globl BOOT_PUT32
BOOT_PUT32:
    str r1,[r0]
    bx lr

.globl start_mmu
start_mmu:
    mov r2,#0
    mcr p15,0,r2,c8,c7,0 ;@ invalidate tlb
    mcr p15,0,r2,c7,c10,4 ;@ DSB ??

     ;@ Domain 0 Manager access. No permissions checked for access
    MRC p15, 0, r2, c3, c0, 0 ;@ Read DACR into Rt
    ORR r2, #3
    MCR p15, 0, r2, c3, c0, 0 ;@ Write Rt to DACR

    MCR p15,0,r0,c2,c0,0 ;@ Write Rt to TTBR0
    MCR p15, 0, r0, c2, c0, 1 ;@ Write Rt to TTBR1

    MRC p15, 0, r2, c1, c0, 0 ;@ Read SCTLR into Rt
    orr r2,r2,r1
    MCR p15, 0, r2, c1, c0, 0 ;@ Write Rt to SCTLR.

    bx lr
