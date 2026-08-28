/*
 * GWKernel linker script - Phase 1
 * Physical load address chosen to be high enough and page-aligned.
 * The bootloader relocates based on p_paddr.
 */

ENTRY(_start)

SECTIONS
{
    /* Load at 1 MiB physical for simplicity and to avoid low memory */
    . = 0x100000;

    .text : ALIGN(4096) {
        *(.text.boot)
        *(.text .text.*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096) {
        *(.data .data.*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note.GNU-stack)
        *(.comment)
    }
}
