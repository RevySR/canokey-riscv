OUTPUT_ARCH(riscv)
ENTRY(_start)
/* Keep the running CanoKey image at 0x80300000 intact during SD tests. */
MEMORY { SRAM (rwx) : ORIGIN = 0x80280000, LENGTH = 256K }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > SRAM
    .rodata : { *(.rodata .rodata.*) } > SRAM
    .data : { *(.data .data.*) *(.sdata .sdata.*) } > SRAM
    .bss (NOLOAD) : {
        . = ALIGN(8); __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(8); __bss_end = .;
    } > SRAM
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
    ASSERT(__bss_end < __stack_top - 8192, "SD probe stack overlap")
}
