OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
    . = 0x20000;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    ASSERT(. < 0x28000, "FEL tool exceeds scratch SRAM")
}
