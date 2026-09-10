/* SPDX-License-Identifier: Apache-2.0 */
.section .boothead,"a",@progbits
.word 0x0400006f
.ascii "eGON.BT0"
.word 0x5f0a6c39
.word 0
.word 48
.ascii "3000"
.word 0x20000
.word 0x20000
.word 0
.byte 0,0,51,46,48,46,48,0
.zero 16
.section .text.start,"ax",@progbits
.globl _start
_start:
    csrw mie, zero
    csrci mstatus, 8
    li t0, 0x638000
    csrs 0x7c0, t0
    li t0, 0x70033
    csrw 0x7c2, t0
    csrw 0x7c1, zero
    fence.i
    la sp, __stack_top
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la t0, boot_trap
    csrw mtvec, t0
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0,t1,2f
    sw zero,0(t0)
    addi t0,t0,4
    j 1b
2:  call boot_main
3:  j 3b
.balign 4
boot_trap:
    la sp, __stack_top
    call boot_fault
    j 3b
