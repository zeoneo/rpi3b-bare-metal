.section ".text.boot"
 
// Make _start global.
.globl _start

.balign 4
_start:
    bl main
    