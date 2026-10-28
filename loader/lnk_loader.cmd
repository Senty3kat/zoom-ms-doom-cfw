/* Doom CFW loader: everything in shared RAM, entry stub first (its address is patched
   into the OS boot step). The stack is set by doom_hook (0x8001FF00 down), the mailbox
   sits at 0x8001FFC0. No run-time library, no .cinit: nothing needs initial values. */
-e doom_hook
--ram_model
MEMORY
{
    SHRAM: o = 0x80000000  l = 0x00018000
}
SECTIONS
{
    .text:entry > 0x80000000
    .text       > SHRAM
    .const      > SHRAM
    .far        > SHRAM
    .bss        > SHRAM
    .fardata    > SHRAM
    .neardata   > SHRAM
    .rodata     > SHRAM
    .switch     > SHRAM
}
