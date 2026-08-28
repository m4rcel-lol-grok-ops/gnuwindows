/*
 * Linker script for UEFI PE/COFF application (x86_64)
 * Used with clang + lld
 */

ENTRY(efi_main)

SECTIONS
{
    . = 0x0;

    .text : {
        *(.text .text.*)
    }

    .data : {
        *(.data .data.*)
        *(.rodata .rodata.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note.GNU-stack)
        *(.comment)
        *(.reloc)
    }
}
