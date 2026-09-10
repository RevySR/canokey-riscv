/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    addi sp,sp,-16
    sw ra,12(sp)
    sw s0,8(sp)
    csrr t0,mstatus
    sw t0,4(sp)
    csrci mstatus,8
    mv s0,sp
    li sp,0x40100000
    fence.i
    call aes_test_main
    mv sp,s0
    lw t0,4(sp)
    csrw mstatus,t0
    lw s0,8(sp)
    lw ra,12(sp)
    addi sp,sp,16
    ret
