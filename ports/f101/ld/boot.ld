OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { SRAM (rwx) : ORIGIN = 0x20000, LENGTH = 32K }
SECTIONS {
    .boothead : { KEEP(*(.boothead)) } > SRAM
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > SRAM
    .rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > SRAM
    .data : {
        . = ALIGN(16); PROVIDE(__global_pointer$ = . + 0x800);
        *(.data .data.*) *(.sdata .sdata.*)
    } > SRAM
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > SRAM
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
    ASSERT(__bss_end <= __stack_top - 8192, "Insufficient SPL stack space")
}
