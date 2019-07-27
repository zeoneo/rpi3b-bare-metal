.global handle_syscall
.global swi_handler
    @ stmia	sp,{r0-lr}^
    bl handle_syscall
    @ ldmia	sp, {r0-lr}^
    bx lr