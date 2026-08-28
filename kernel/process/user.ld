ENTRY(user_program_start)
SECTIONS
{
    . = 0x400000;
    .text : {
        *(.text)
        *(.rodata*)
    }
}
