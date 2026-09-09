/* SPDX-License-Identifier: Apache-2.0 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    csrw mie, zero
    csrci mstatus, 8
    li t0, 0x70033
    csrw 0x7c2, t0
    csrw 0x7c1, zero
    fence.i
    la sp, __stack_top
    la t0, trap_entry
    csrw mtvec, t0
#ifdef CANOKEY_K230D
    la tp, __tls_base
    la t0, __tls_bss_start
    la t1, __tls_bss_end
4:
    bgeu t0, t1, 5f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 4b
5:
#endif
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
#if K230D_CACHE_ENABLED
    /* K230 U-Boot improving_cpu_performance(): use the board's L2 RAM
     * latency settings, not the newer C908V2 manual's example values.
     * Enable snooping before L1. Keep the SoC physical memory attributes:
     * SRAM is normal memory; peripheral MMIO must remain non-cacheable.
     * USB and SDIO use CPU FIFO PIO, not DMA buffers. */
    li t0, 0xe0000009
    csrw 0x7c3, t0
    li t0, 0x638000
    csrw 0x7c0, t0
    li t0, 0x6e30c
    csrw 0x7c5, t0
    csrsi 0x7f3, 1
    fence iorw, iorw
    li t0, 0x11ff
    csrw 0x7c1, t0
    fence.i
#endif
    call firmware_main
3:
    j 3b
.balign 4
trap_entry:
    la t0, firmware_state
    csrr t1, mcause
    sd t1, 32(t0)
    csrr t1, mepc
    sd t1, 40(t0)
    csrr t1, mtval
    sd t1, 48(t0)
    li t1, -1
    sd t1, 8(t0)
    j 3b
