;@ Disable MMU
.globl disable_mmu
disable_mmu:
    MRC p15, 0, r1, c1, c0, 0 
    BIC r1, r1, #0x1
    MCR p15, 0, r1, c1, c0, 0
    bx lr

@ Disable L1 Caches
.globl disable_l1_caches
disable_l1_caches:
    MRC p15, 0, r1, c1, c0, 0 
    BIC r1, r1, #(0x1 << 12)
    BIC r1, r1, #(0x1 << 2)
    MCR p15, 0, r1, c1, c0, 0
    bx lr

@ Invalidate L1 Caches
@ Invalidate Instruction cache MOV r1, #0
.globl invalidate_l1_icache
invalidate_l1_icache:
    MCR p15, 0, r1, c7, c5, 0
    bx lr

@ Invalidate Data cache
@ to make the code general purpose, @ cache size first and loop through
.globl invalidate_l1_dcache
invalidate_l1_dcache:
    MRC p15, 1, r0, c0, c0, 0
    LDR r3, =#0x1ff
    AND r0, r3, r0, LSR #13
    MOV r1, #0
way_loop:
    MOV r3, #0
set_loop:
    MOV r2, r1, LSL #30
    ORR r2, r3, LSL #5
    MCR p15, 0, r2, c7, c6, 2
    ADD r3, r3, #1
    CMP r0, r3
    BGT set_loop
    ADD r1, r1, #1
    CMP r1, #4
    BNE way_loop
    bx lr

@ Invalidate TLB
.globl invalidate_tlb
invalidate_tlb:
    MCR p15, 0, r1, c8, c7, 0
    bx lr

.globl enable_branch_prediction
enable_branch_prediction:
    MOV r1, #0
    MRC p15, 0, r1, c1, c0, 0
    ORR r1, r1, #(0x1 << 11)
    MCR p15, 0, r1, c1, c0, 0
    bx lr

@ Enable D-side Prefetch
.globl enable_data_prefetch
enable_data_prefetch:
    MRC p15, 0, r1, c1, c0, 1
    ORR r1, r1, #(0x1 <<2)
    MCR p15, 0, r1, c1, c0, 1;
    DSB
    ISB
    bx lr
