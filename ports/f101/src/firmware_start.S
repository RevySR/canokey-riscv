/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    csrw mie, zero
    csrci mstatus, 8
    la sp, __stack_top
    la tp, __tls_base
    la t0, trap_entry
    csrw mtvec, t0
    li t0, 0x408000
    csrs 0x7c0, t0
    /* Synchronize memory before enabling the C907 caches. */
    li t0, 0x70033
    csrw 0x7c2, t0
    fence
    fence.i
    li t0, 0x11ff
    csrw 0x7c1, t0
    li t0, 0x16e30c
    csrw 0x7c5, t0
    fence
    la t0, __tls_bss_start
    la t1, __tls_bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  la t0, __bss_start
    la t1, __bss_end
3:  bgeu t0, t1, 4f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 3b
4:  call firmware_main
5:  j 5b
.balign 4
trap_entry:
    csrw mie, zero
    la sp, __stack_top
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call f101_trap
    j 5b
