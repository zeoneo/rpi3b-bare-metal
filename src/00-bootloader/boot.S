// To keep this in the first portion of the binary.
.section ".text.boot"
 
// Make _start global.
.globl Start
.globl branch_to_kernel
 
// Entry point for the kernel.
// r15 -> should begin execution at 0x8000.
// r0 -> 0x00000000
// r1 -> 0x00000C42
// r2 -> 0x00000100 - start of ATAGS
// preserve these registers as argument for kernel_main
Start:
	// Setup the stack.
	mov sp, #0x8000
 

 .relocate:
	// copy from r3 to r4.
	mov	r3, #0x8000
	ldr	r4, =_start
	ldr	r9, =_data_end

1:
	// Load multiple from r3, and store at r4.
	ldmia	r3!, {r5-r8}
	stmia	r4!, {r5-r8}

	// If we're still below file_end, loop.
	cmp	r4, r9
	blo	1b


	// Clear out bss.
	ldr r4, =_bss_start
	ldr r9, =_bss_end
	mov r5, #0
	mov r6, #0
	mov r7, #0
	mov r8, #0
	b       2f
 
1:
	// store multiple at r4.
	stmia r4!, {r5-r8}
 
	// If we are still below bss_end, loop.
2:
	cmp r4, r9
	blo 1b
 
	// Call kernel_main
	ldr r3, =kernel_main
	blx r3
 
	// halt
halt:
	wfe
	b halt

.globl BRANCHTO
BRANCHTO:
    bx r0

.globl dummy
dummy:
    bx lr