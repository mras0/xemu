        org 0x7c3e

        jmp 0:start
start:
        xor     ax,ax
        mov     ds,ax
        mov     si,message
.loop:
        lodsb
        test    al,al
        jz      .done
        mov     bx,7
        mov     ah,0x0e
        int     0x10
        jmp     .loop
.done:
        xor     ax,ax
        int     0x16
        int     0x19

message:
        db 'Non-bootable disk, press any key to reboot...',0
