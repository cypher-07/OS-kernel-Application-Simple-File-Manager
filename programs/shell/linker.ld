ENTRY(_start)
OUTPUT_FORMAT(binary)
SECTIONS
{
    . = 0x400000;
    .text : { *(.asm) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}