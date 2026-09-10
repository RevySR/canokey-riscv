OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
    . = 0x28000;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) }
    .data : {
        . = ALIGN(16); PROVIDE(__global_pointer$ = . + 0x800);
        *(.data .data.*) *(.sdata .sdata.*)
    }
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    }
    ASSERT(. < 0x2e000, "PSRAM FEL initializer exceeds SRAM budget")
}
