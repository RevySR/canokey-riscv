/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    addi sp, sp, -16
    sw ra, 12(sp)
    csrr t0, mstatus
    sw t0, 8(sp)
    csrci mstatus, 8
    fence.i
    call fel_main
    lw t0, 8(sp)
    csrw mstatus, t0
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
