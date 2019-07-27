.globl move_to_user_mode
move_to_user_mode:
    bx lr

.global move_to_kernel_mode
move_to_kernel_mode:
    bx lr