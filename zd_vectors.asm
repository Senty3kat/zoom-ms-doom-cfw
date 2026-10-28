; Doom CFW: interrupt service table (ISTP points here, zd_pedal.c sets it up).
; Only INT8 is used: GPIO bank 1 (event 41), the knob encoders, as in the Zoom OS.
; Every vector is one 32-byte fetch packet, so this file is assembled with --no_compress.

        .sect   ".text:vectors"
        .align  1024
        .global zd_vectors
        .ref    zd_gpio_isr

RETURN  .macro  reg
        B       .S2     reg
        NOP     5
        NOP
        NOP
        NOP
        NOP
        NOP
        NOP
        .endm

zd_vectors:
        RETURN  IRP                             ; 0 reset (not taken from here)
        RETURN  NRP                             ; 1 NMI
        RETURN  IRP                             ; 2 reserved
        RETURN  IRP                             ; 3 reserved
        RETURN  IRP                             ; 4
        RETURN  IRP                             ; 5
        RETURN  IRP                             ; 6
        RETURN  IRP                             ; 7
        STW     .D2T2   B0, *B15--[2]           ; 8: GPIO bank 1 -> zd_gpio_isr
        MVKL    .S2     zd_gpio_isr, B0
        MVKH    .S2     zd_gpio_isr, B0
        B       .S2     B0
        LDW     .D2T2   *++B15[2], B0
        NOP     4
        NOP
        NOP
        RETURN  IRP                             ; 9
        RETURN  IRP                             ; 10
        RETURN  IRP                             ; 11
        RETURN  IRP                             ; 12
        RETURN  IRP                             ; 13
        RETURN  IRP                             ; 14
        RETURN  IRP                             ; 15
