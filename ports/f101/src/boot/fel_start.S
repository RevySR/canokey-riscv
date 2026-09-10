/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start, "ax", @progbits
.globl _start
_start:
    fence.i
    addi sp, sp, -32
    sw ra, 28(sp)
    sw gp, 24(sp)
    csrr t0, mstatus
    sw t0, 20(sp)
    csrr t0, 0x7c0
    sw t0, 16(sp)
    csrr t0, mtvec
    sw t0, 12(sp)
    csrr t0, mscratch
    sw t0, 8(sp)
    csrr t0, 0x7c1
    sw t0, 4(sp)
    csrw mscratch, sp
    csrci mstatus, 8
    li t0, 0x638000
    csrs 0x7c0, t0
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la t0, fel_boot_trap
    csrw mtvec, t0
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  fence.i
    call fel_boot_main
return_to_fel:
    li t0, 0x70033
    csrw 0x7c2, t0
    .word 0x01b0000b
    fence.i
    lw t0, 4(sp)
    csrw 0x7c1, t0
    lw t0, 8(sp)
    csrw mscratch, t0
    lw t0, 12(sp)
    csrw mtvec, t0
    lw t0, 16(sp)
    csrw 0x7c0, t0
    lw t0, 20(sp)
    csrw mstatus, t0
    lw gp, 24(sp)
    lw ra, 28(sp)
    addi sp, sp, 32
    ret
.balign 4
fel_boot_trap:
    csrr sp, mscratch
    li t0, 0x27f00
    li t1, 0x5053524d
    sw t1, 0(t0)
    sw zero, 4(t0)
    csrr t1, mcause
    sw t1, 8(t0)
    csrr t1, mepc
    sw t1, 12(t0)
    j return_to_fel
