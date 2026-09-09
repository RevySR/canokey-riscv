OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { SRAM (rwx) : ORIGIN = 0x80300000, LENGTH = 512K }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > SRAM
    .rodata : { *(.rodata .rodata.*) } > SRAM
    .data : { *(.data .data.*) *(.sdata .sdata.*) } > SRAM
    .tdata : {
        . = ALIGN(16); __tls_base = .;
        *(.tdata .tdata.*)
    } > SRAM
    .tbss (NOLOAD) : {
        __tls_bss_start = .; *(.tbss .tbss.*)
        . = ALIGN(8); __tls_bss_end = .;
    } > SRAM
    /* Reserve the static TLS block; ELF TLS NOBITS does not advance the
     * non-TLS location counter automatically. */
    .bss (ADDR(.tbss) + SIZEOF(.tbss)) (NOLOAD) : {
        . = ALIGN(8); __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(16); __bss_end = .;
    } > SRAM
    __heap_start = __bss_end;
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
    __heap_end = __stack_top - 32768;
    ASSERT(__heap_end >= __heap_start + 32768, "Insufficient heap/stack SRAM")
}
