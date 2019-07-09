.equ SYNC_INVALID_EL1t, 0
.equ IRQ_INVALID_EL1t, 1 
.equ FIQ_INVALID_EL1t, 2 
.equ ERROR_INVALID_EL1t, 3 

.equ SYNC_INVALID_EL1h, 4 
.equ FIQ_INVALID_EL1h, 5 
.equ ERROR_INVALID_EL1h, 6 

.equ FIQ_INVALID_EL0_64, 7 
.equ ERROR_INVALID_EL0_64, 8 

.equ SYNC_INVALID_EL0_32, 9 
.equ IRQ_INVALID_EL0_32, 10 
.equ FIQ_INVALID_EL0_32, 11 
.equ ERROR_INVALID_EL0_32, 12 

.equ SYNC_ERROR, 13 
.equ SYSCALL_ERROR, 14 
.equ DATA_ABORT_ERROR, 15 

.macro handle_invalid_entry el, type
    b   err_hang
.endm

.macro	exception_entry	label
    .align  7
        b   \label
.endm

.align	32
.globl exception_table 
exception_table:
    exception_entry	sync_invalid_el1t			// Synchronous EL1t
    exception_entry	irq_invalid_el1t			// IRQ EL1t
    exception_entry	fiq_invalid_el1t			// FIQ EL1t
    exception_entry	error_invalid_el1t			// Error EL1t

    exception_entry	sync_invalid_el1h			// Synchronous EL1h
    exception_entry	el1_irq					// IRQ EL1h
    exception_entry	fiq_invalid_el1h			// FIQ EL1h
    exception_entry	error_invalid_el1h			// Error EL1h

    exception_entry	el0_sync				// Synchronous 64-bit EL0
    exception_entry	el0_irq					// IRQ 64-bit EL0
    exception_entry	fiq_invalid_el0_64			// FIQ 64-bit EL0
    exception_entry	error_invalid_el0_64			// Error 64-bit EL0

    exception_entry	sync_invalid_el0_32			// Synchronous 32-bit EL0
    exception_entry	irq_invalid_el0_32			// IRQ 32-bit EL0
    exception_entry	fiq_invalid_el0_32			// FIQ 32-bit EL0
    exception_entry	error_invalid_el0_32			// Error 32-bit EL0

sync_invalid_el1t:
    handle_invalid_entry 1, SYNC_INVALID_EL1t

irq_invalid_el1t:
    handle_invalid_entry 1, IRQ_INVALID_EL1t

fiq_invalid_el1t:
    handle_invalid_entry 1, FIQ_INVALID_EL1t

error_invalid_el1t:
    handle_invalid_entry 1, ERROR_INVALID_EL1t

sync_invalid_el1h:
    handle_invalid_entry 1, SYNC_INVALID_EL1h

fiq_invalid_el1h:
    handle_invalid_entry 1, FIQ_INVALID_EL1h

error_invalid_el1h:
    handle_invalid_entry 1, ERROR_INVALID_EL1h

fiq_invalid_el0_64:
    handle_invalid_entry 0, FIQ_INVALID_EL0_64

error_invalid_el0_64:
    handle_invalid_entry 0, ERROR_INVALID_EL0_64

sync_invalid_el0_32:
    handle_invalid_entry  0, SYNC_INVALID_EL0_32

irq_invalid_el0_32:
    handle_invalid_entry  0, IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
    handle_invalid_entry  0, FIQ_INVALID_EL0_32

error_invalid_el0_32:
    handle_invalid_entry  0, ERROR_INVALID_EL0_32


el1_irq:
    bl	err_hang

el0_irq:
    bl	err_hang

el0_sync:
    b err_hang

el0_svc:
    b err_hang

ni_sys:
    b err_hang

el0_da:
    b err_hang

.globl err_hang
    err_hang:
        b err_hang