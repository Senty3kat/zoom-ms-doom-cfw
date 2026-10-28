/* Doom, stage 1: bare metal in the emulator runner (no pedal OS).
   Plenty of memory here; the pedal budget comes in stage 2. The IWAD is loaded by the
   runner at 0xC3000000 (outside these ranges). */
-stack 0x40000
-heap  0x01000000
-e _c_int00
MEMORY
{
    L2:    o = 0x11800000  l = 0x00040000
    SDRAM: o = 0xC0000000  l = 0x03000000
}
SECTIONS
{
    .text       > SDRAM
    .stack      > SDRAM
    .cinit      > SDRAM
    .const      > SDRAM
    .rodata     > SDRAM
    .switch     > SDRAM
    .bss        > SDRAM
    .neardata   > SDRAM
    .fardata    > SDRAM
    .far        > SDRAM
    .data       > SDRAM
    .sysmem     > SDRAM
    .cio        > SDRAM
    .init_array > SDRAM
    .args       > SDRAM
}
