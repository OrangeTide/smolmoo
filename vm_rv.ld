/* vm_rv.ld : linker script for smolmoo RISC-V RV32 verb programs */
/* 128 KB address space: vectors 0x0-0x3FF (host), code at 0x400+ */

OUTPUT_FORMAT("elf32-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000400, LENGTH = 0x1FC00
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    .data : {
        *(.data .data.*)
    } > RAM

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
        __moo_arena = .;
    } > RAM
}
