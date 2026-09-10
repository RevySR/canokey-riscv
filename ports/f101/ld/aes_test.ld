OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
    . = 0x40010000;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.sdata .sdata.*) }
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    }
    ASSERT(. < 0x400f0000, "AES test exceeds PSRAM code/data budget")
}
