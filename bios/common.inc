HACK_PORT EQU 0xE9
BDA_SEG EQU 0x40

; https://wiki.osdev.org/Memory_Map_(x86)#BIOS_Data_Area_(BDA)
struc   BDA
        bda_fill0               resb 0x10
        bda_eqiupment           resw 1          ; 10h Installed hardware
        bda_postStatus          resb 1          ; 12h POST status
        bda_memSizeKB           resw 1          ; 13h Base memory size in KB
        bda_scratch1            resb 1          ; 15h
        bda_scratch2            resb 1          ; 16h
        bda_kbdStatus1          resb 1          ; 17h Keyboard status flag 1
        bda_kbdStatus2          resb 1          ; 18h Keyboard status flag 2
        bda_kbdNumWorkspace     resb 1          ; 19h Keyboard: Alt-nnn keypad workspace
        bda_kbdNextChar         resw 1          ; 1Ah Keyboard: ptr to next character in keyboard buffer
        bda_kbdNextFree         resw 1          ; 1Ch Keyboard: ptr to first free slot in keyboard buffer
        bda_kbdBuffer           resw 16         ; 1Eh
        bda_fill1               resb 11
        bda_videoMode           resb 1          ; 49h Display mode
        bda_videoColumns        resw 1          ; 4Ah Number of columns in text mode
        bda_videoPageSize       resw 1          ; 4Ch Page size in bytes
        bda_videoPageStart      resw 1          ; 4Eh Current page start address in regen buffer
        bda_videoCursorPos      resb 16         ; 50h Cursor position (col, row) for eight pages, 0 based
        bda_videoCursorType     resw 1          ; 60h Cursor type, 6845 compatible, hi=startline, lo=endline
        bda_videoCurrentPage    resb 1          ; 62h Current page number
        bda_videoBaseAddress    resw 1          ; 63h CRT controller base address (color=3D4h/mono=3B4h)
        bda_videoModeSelection  resb 1          ; 65h Current setting of mode select register (3D8h/3B8h)
        bda_videoPalette        resb 1          ; 66h Current setting of CGA palette register (3D9h)
                                resd 1          ; 67h POST real mode re-entry point
                                resb 1          ; 6Bh POST last unexpected interrupt
        bda_timerTicks          resd 1          ; 6Ch Timer ticks since midnight
        bda_timerOverflow       resb 1          ; 70h Timer overflow (if counted past midnight)
        bda_ctrlBreakFlag       resb 1          ; 71h Ctrl-Break flag: bit 7=1
        bda_postResetFlag       resw 1          ; 72h POST reset flag:
        bda_hdLastStatus        resb 1          ; 74h Fixed disk last operation status
        bda_hdCount             resb 1          ; 75h Fixed disk: number of fixed disk drives
        bda_fill2               resb 14
        bda_videoCharRows       resb 1          ; 84h Video EGA/MCGA/VGA rows on screen minus one
                                resb 0xc0-0x85
        bda_tempExtMem          resw 1          ; Normally reserved
endstruc

%macro CHECK_OFFSET 2
        %if %1 != %2
                %assign wrongOffset %1
                %assign expectedOffset %2
                %error Wrong structure offset wrongOffset expected expectedOffset
        %endif
%endmacro

        CHECK_OFFSET bda_eqiupment,0x10
        CHECK_OFFSET bda_memSizeKB,0x13
        CHECK_OFFSET bda_kbdBuffer,0x1E
        CHECK_OFFSET bda_videoPalette,0x66
        CHECK_OFFSET bda_timerOverflow,0x70
        CHECK_OFFSET bda_hdCount,0x75
        CHECK_OFFSET bda_videoCharRows,0x84
        CHECK_OFFSET bda_tempExtMem,0xC0

%macro DEBUG_BREAK 0
        push    ax
        push    dx
        mov     ax,0x8abc
        mov     dx,ax
        not     ax
        out     dx,ax
        pop     dx
        pop     ax
%endmacro

; Interrupt stack frame *in dispatch function* (isf_RETADDR)
struc   IntStackFrame
        isf_RETADDR     resw 1
        isf_ES          resw 1
        isf_DS          resw 1
        isf_DI          resw 1
        isf_SI          resw 1
        isf_BP          resw 1
        isf_DX          resw 1
        isf_CX          resw 1
        isf_BX          resw 1
        isf_AX          resw 1
        isf_FLAGS       resw 1
        isf_CS          resw 1
        isf_IP          resw 1
endstruc

%macro  PUSH_ALL 0
        push    ax
        push    bx
        push    cx
        push    dx
        push    bp
        push    si
        push    di
        push    ds
        push    es
%endmacro

%macro POP_ALL 0
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        pop     dx
        pop     cx
        pop     bx
        pop     ax
%endmacro


