/* Doom CFW (stage 2): Doom owns SDRAM after loader.c stopped the OS.
   0xC0000000..0xC03FFFFF: code, data, stack, heap (Doom's 2 MB zone is malloc'ed here).
   0xC0400000..0xC07FFFFF: the IWAD, inflated by the loader at SDRAM_END - size.
   L2 (0x11800000..) and shared RAM stay untouched: the OS's LCD driver and its data
   live in L2, the loader and its mailbox in shared RAM. */
-stack 0x00040000
-heap  0x002C0000
-e _c_int00
MEMORY
{
    SDRAM: o = 0xC0000000  l = 0x00400000
}
SECTIONS
{
    .text       > SDRAM
    .const      > SDRAM
    .rodata     > SDRAM
    .switch     > SDRAM
    .cinit      > SDRAM
    .init_array > SDRAM
    .fardata    > SDRAM
    .neardata   > SDRAM
    .data       > SDRAM
    .far        > SDRAM
    .bss        > SDRAM
    .cio        > SDRAM
    .args       > SDRAM
    .stack      > SDRAM
    .sysmem     > SDRAM
}
