; Doom CFW: entry from the OS boot step (callp at 0xC00BB144 is patched to doom_hook).
; Shows the boot logo with the original function, then leaves the OS for good:
; interrupts off, stack moved to the top of shared RAM, loader_main never returns.

BOOT_SCREEN     .set    0xC00CD680              ; MS-70CDR 2.10 boot logo + version line
STACK_TOP       .set    0x8001FF00              ; mailbox lives above it (0x8001FFC0)

        .sect   ".text:entry"
        .global doom_hook
        .global loader_main

doom_hook:
        STW     .D2T2   B3, *B15--[2]
        MVKL    .S2     BOOT_SCREEN, B0
        MVKH    .S2     BOOT_SCREEN, B0
        B       .S2     B0
        ADDKPC  .S2     ret_logo, B3, 4
ret_logo:
        DINT                                    ; GIE off: no OS task or ISR runs again
        MVK     .S2     0, B0
        MVC     .S2     B0, IER
        MVKL    .S2     STACK_TOP, B15
        MVKH    .S2     STACK_TOP, B15
        MVKL    .S2     loader_main, B0
        MVKH    .S2     loader_main, B0
        B       .S2     B0
        ADDKPC  .S2     hang, B3, 4
hang:
        B       .S1     hang
        NOP     5
