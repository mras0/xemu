        bits 16
        cpu 8086

%include "common.inc"

BIOS_SIZE EQU 0x800
BIOS_SEG EQU 0xC000

        db 0x55,0xAA,BIOS_SIZE/512

Entry:
        push    ax
        push    ds
        xor     ax,ax
        mov     ds,ax
        mov     word [ds:0x10*4+0],Int10h_VideoInt
        mov     word [ds:0x10*4+2],BIOS_SEG
        mov     ax,3
        int     0x10
        pop     ds
        pop     ax
        retf

%include "int10h.asm"

FatalError:
        out     HACK_PORT,AL
        cli
        hlt
        jmp     FatalError


        times BIOS_SIZE-($-$$) hlt
