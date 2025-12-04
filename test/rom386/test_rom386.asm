;; Can be assembled as either a ".COM" style program (will reloc to 0x8000:0) or a "BIOS" (0xF000:0)
;; Done when 0xFF written to POST_PORT (like test386.asm)
;; HLT is executed on error/completion

;; CONFIGURATION ;;;;;;;;;;;;;;;;;;;;;;;;

; Same as test386.asm
DEBUG_PORT EQU 0xE9     ; Messages are printed here if non-zero (must be 8-bit port)
POST_PORT EQU 0x190     ; Progress is reported here

CGA_DISPLAY EQU 1       ; Initialize CGA display and output to B800:0000 (if run as COM program rely on int10h for setup)
SER_OUTPUT EQU 1        ; Write output to 0x3f8

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


; TODO/Test
; Check if PMODE_EXIT reloads are necessary (=is E/RW cached in segment descriptor - probably yes)
; NT set in DosBox...
; Manual says you need JMPF, but is RET[F] or other control transfers OK?
; Dosbox doesn't like SS=0 in TSS for v86 test (at start)
; Dosbox/Qemu allows unreal mode in v86!
; TODO: IO permission bitmap test
; TODO: Test "NT" flag
; TODO: Which flags are allowed to be pushed/popped
; TODO: INT allowed to interrupt level 3...

        cpu 386
        bits 16
        org 0

ROM_SIZE EQU 0x10000

INT_HANDLER_COUNT EQU 32
INT_HANDLER_SIZE EQU 4

DISPLAY_CURSOR_ADDR EQU 0x400
IDT_BASE EQU 0x1000
GDT_BASE EQU 0x2000
TSS_BASE EQU 0x2F00
SCRATCH EQU 0x3000

GDT_SEL_CODE32 EQU 0x08
GDT_SEL_DATA32 EQU 0x10
GDT_SEL_ORIG16 EQU 0x18
GDT_SEL_DATA16 EQU 0x20
GDT_SEL_TSS    EQU 0x28

MAX_IO_PORT EQU 0x400

TSS_SS0  EQU 0x08
TSS_ESP0 EQU 0x04
TSS_IOPB EQU 0x66
TSS_BASE_SIZE EQU 0x6C
TSS_SIZE EQU 0x6C+4*((MAX_IO_PORT+31)/32)
TSS_IO_BITMAP_START EQU TSS_BASE_SIZE

EXCEPTION_UD EQU 6
EXCEPTION_GP EQU 13

EFLAGS_BIT_IOPL EQU 12
EFLAGS_MASK_VM EQU 1<<17

HAS_ERROR_CODE_MASK EQU 1<<8|1<<10|1<<11|1<<12|1<<13|1<<14|1<<17|1<<30

%macro PMODE_EXIT 0
        bits 32
        push    eax
        ; Reload writable selectors into DS/ES (in case they pointed at GDT_SEL_ORIG16)
        mov     ax,GDT_SEL_DATA16
        mov     ds,ax
        mov     es,ax
        pop     eax
        jmp     GDT_SEL_ORIG16:%%code16
        bits 16
%%code16:
        push    eax
        mov     eax,cr0
        and     al,0xfe
        mov     cr0,eax
        pop     eax
%endmacro

%macro  GET_ADDR_REL 2
        call    %%lab
%%lab:
        pop     %1
        add     %1,%2-%%lab
%endmacro

%macro  GET_LINEAR_BASE 1 ; Requires DS.base = 0
        mov     %1,[GDT_BASE+GDT_SEL_ORIG16+2]
        and     %1,0xffffff
%endmacro

%macro  GET_REAL_SEG 1 ; Requires DS.base = 0
        mov     %1,[GDT_BASE+GDT_SEL_ORIG16+2]
        shr     %1,4
        and     %1,0xffff
%endmacro

%macro SET_HANDLER 2
        push    ebx
        %if __?BITS?__==16
        push    ds
        xor     bx,bx
        mov     ds,bx
        %endif
        GET_LINEAR_BASE ebx
        add     ebx,%2
        mov     [IDT_BASE+(%1)*8],bx
        shr     ebx,16
        mov     [IDT_BASE+(%1)*8+6],bx
        %if __?BITS?__==16
        pop     ds
        %endif
        pop     ebx
%endmacro

%macro RESTORE_HANDLER 1
        SET_HANDLER  %1,ProtIntHandlers+(%1)*INT_HANDLER_SIZE
%endmacro


%macro DEBUG_BREAK 0
        xchg    bx,bx   ; Bochs
        push    ax
        push    dx
        mov     dx,0x8abc
        mov     ax,~0x8abc
        out     dx,ax
        pop     dx
        pop     ax
%endmacro

%macro  POST 1
        push    ax
        push    dx
        mov     dx,POST_PORT
        mov     al,%1
        out     dx,al
        pop     dx
        pop     ax
%endmacro

%macro CHECK_CC 1
        j%1     %%ok
        %if __?BITS?__==16
        call    RModeFail
        %else
        call    PModeFail
        %endif
%%ok:
%endmacro

%macro CHECK_EQ 2
        cmp     %1,%2
        CHECK_CC e
%endmacro

%macro CALL_V86 1
        mov     ebp,%1
        call    V86Call
%endmacro

COM_SEG EQU 0x8000
ComEntry:
        %if CGA_DISPLAY
        mov     ax,0x03
        int     0x10
        mov     ah,0x01
        mov     cx,0x0100
        int     0x10
        xor     ax,ax
        mov     es,ax
        mov     word [es:DISPLAY_CURSOR_ADDR],0
        mov     bl,1
        %endif
        cli
        cld
        mov     ax,COM_SEG
        mov     es,ax
        xor     di,di
        mov     si,0x100
        mov     cx,(65536-0x100)/4
        rep     movsd

        ; For the sake of DosBox use iret to clear NT flag ?!?!?!
        ;jmp     COM_SEG:BiosEntry
        push    0
        push    COM_SEG
        push    BiosEntry
        iret

BiosEntry:
	cli
        cld
	xor	ax,ax
	mov	ss,ax
        mov     ax,0x8000
        mov     sp,ax

        ; Mask all interrupts
        mov     al,0xff
        out     0x21,al
        out     0xa1,al

        %if CGA_DISPLAY
        test    bl,bl
        jne     .noinit
        call    DisplayInit
.noinit:
        %endif
        %if SER_OUTPUT
        call    SerialInit
        %endif

        push    cs
        pop     ds
        mov     si,HelloMsg
        call    PrintString

        mov     dx,RealIntHandlers
        mov     cx,INT_HANDLER_COUNT
        xor     ax,ax
        mov     es,ax
        xor     di,di
.initint:
        mov     ax,dx
        stosw
        mov     ax,cs
        stosw
        add     dx,INT_HANDLER_SIZE
        loop    .initint

        xor     ax,ax
        mov     ds,ax
        mov     es,ax
        mov     di,IDT_BASE
        mov     cx,INT_HANDLER_COUNT
        mov     ebx,cs
        shl     ebx,4
        add     ebx,ProtIntHandlers
.protidt:
        mov     ax,bx
        stosw   ; offset low
        mov     ax,0x0008 ; segment selector
        stosw
        mov     ax,0x8E00 ; type (present, DPL=0, 32-bit interrupt gate)
        stosw
        mov     eax,ebx
        shr     eax,16
        stosw
        add     ebx,INT_HANDLER_SIZE
        loop    .protidt

        mov     di,GDT_BASE
        push    cs
        pop     ds
        mov     si,ProtGDT
        mov     cx,(ProtGDTEnd-ProtGDT)/4
        rep     movsd

        ; Fix offset of "ORIG16" descriptor
        mov     eax,cs
        shl     eax,4
        or      [es:GDT_BASE+GDT_SEL_ORIG16+2],eax

        ; Clear TSS
        mov     edi,TSS_BASE
        mov     ecx,TSS_SIZE/4
        xor     eax,eax
        rep     stosd
        ; Init IOPB
        mov     word [TSS_BASE+TSS_IOPB],TSS_IO_BITMAP_START

        POST    0

        call    TestRealModeLimit
        call    TestUnrealmode
        call    TestTransitionCPL
        call    TestRealmodeSTR

        POST    1

        lgdt    [cs:ProtGDTR]
        lidt    [cs:ProtIDTR]
        mov     eax,cr0
        or      al,1
        mov     cr0,eax

        mov     eax,GDT_SEL_CODE32
        push    eax
        mov     eax,cs
        shl     eax,4
        add     eax,.pmode
        push    eax
        db 0x66
        retf
.pmode:
        bits    32

        POST    3

        ; Load NULL LDT
        xor     ax,ax
        lldt    ax

        ; SMSW
        smsw    ax
        test    al,1
        CHECK_CC nz

        ; LTR/STR
        mov     ax,GDT_SEL_TSS
        ltr     ax
        str     ebx
        CHECK_EQ ax,bx

        ; SGDT
        mov     ax,GDT_SEL_DATA32
        mov     ds,ax
        mov     edi,SCRATCH
        sgdt    [edi]
        mov     ax,GDT_SEL_ORIG16
        mov     es,ax
        mov     ax,[es:ProtGDTR]
        CHECK_EQ [edi],ax
        mov     eax,[es:ProtGDTR+2]
        CHECK_EQ [edi+2],eax

        ; IRET in protected mode
        xor     eax,eax
        push    eax
        push    dword GDT_SEL_CODE32
        GET_ADDR_REL eax,.iret
        push    eax
        iret
.iret:
        POST    4
        call    TestV86

        ; XXX: Not checked by DosBox!
;        mov     ax,0
;        mov     ds,ax
;        mov     byte [0x1234],42

        mov     si,.msg
        CALL_V86 .printmsg

        mov     si,.alltests
        CALL_V86 .printmsg
        POST    0xff
.stop:
        hlt
        jmp     .stop

        bits    16
.printmsg:
        push    cs
        pop     ds
        jmp     PrintString
.msg:   db      "Back in protected mode with V86 going!",10,0
.alltests:
        db      "All tests done!",10
        db      "-----------------------------------",10,10,0

PModeFail:
        bits    32
        mov     ebp,[esp]
        sub     ebp,5
        bits    16
        PMODE_EXIT
        jmp     _Fail
RModeFail:
        xor     ebp,ebp
        mov     bp,sp
        mov     bp,[bp]
        sub     bp,3
_Fail:
; Address of failure in EBP
        DEBUG_BREAK
        push    ds
        push    cs
        pop     ds
        mov     si,FailMsg
        call    PrintString
        pop     ds
        pushad
        mov     bp,sp
        call    PrintRegs
        popad
        call    PrintSregs
        hlt
.halt:
        jmp     .halt

HelloMsg:
        db "Starting test",10,0
FailMsg:
        db "Test failed",10,0

RealIntHandlers:
        %assign intNo 0
        %rep INT_HANDLER_COUNT
                push    byte intNo
                jmp     short RealIntHandler
                %assign intNo intNo+1
        %endrep

RealIntHandler:
        push    si
        push    ds
        push    cs
        pop     ds
        mov     si,.intstr
        call    PrintString
        pop     ds
        pop     si
        push    ax
        push    bp
        mov     bp,sp
        mov     ax,[bp+4]
        call    PrintByte
        call    PrintNewline
        mov     ax,[bp+8]
        call    PrintWord
        mov     al,':'
        call    PrintChar
        mov     ax,[bp+6]
        call    PrintWord
        call    PrintNewline
        pop     bp
        pop     ax

        pushad
        mov     bp,sp
        call    PrintRegs
        popad
        call    PrintSregs
.halt:
        hlt
        jmp     .halt
.intstr:
        db "Unexpected realmode interrupt #",0

ProtIntHandlers:
        %assign intNo 0
        %rep INT_HANDLER_COUNT
                push    byte intNo
                jmp     short ProtIntHandler
                %assign intNo intNo+1
        %endrep

struc ProtIntFrame
        ;pushad
        pif_SavedRegs   resb    0
        pif_EDI         resd    1
        pif_ESI         resd    1
        pif_EBP         resd    1
        pif_ESP         resd    1
        pif_EBX         resd    1
        pif_EDX         resd    1
        pif_ECX         resd    1
        pif_EAX         resd    1
                        resw    1       ; saved ds
        pif_IntNo       resd    1
        pif_ErrorCode   resb    0       ; Only sometimes
        pif_EIP         resd    1
        pif_CS          resd    1
        pif_Eflags      resd    1
        ; Only for V86
        pif_v86_ESP     resd    1
        pif_v86_SS      resd    1
        pif_v86_ES      resd    1
        pif_v86_DS      resd    1
        pif_v86_FS      resd    1
        pif_v86_GS      resd    1
endstruc

ProtIntHandler:
        PMODE_EXIT
        push    ds
        push    cs
        pop     ds
        pushad
        mov     bp,sp

        ; Handle error code first

        mov     ecx,[bp+pif_IntNo]      ; ECX=interrupt no
        mov     ebx,HAS_ERROR_CODE_MASK
        shr     ebx,cl
        test    bl,1
        jz      .noerrorcode

        mov     si,.msg_errcode
        call    PrintString
        mov     eax,[bp+pif_ErrorCode]
        call    PrintDword
        call    PrintNewline
        add     bp,4    ; Skip error code
.noerrorcode:

        mov     si,.msg
        test    dword [bp+pif_Eflags],EFLAGS_MASK_VM
        jz      .gotmsg
        mov     si,.v86msg
.gotmsg:
        call    PrintString
        mov     al,cl
        call    PrintByte
        call    PrintNewline
        mov     eax,[bp+pif_CS]
        call    PrintWord
        mov     al,':'
        call    PrintChar
        mov     eax,[bp+pif_EIP]
        call    PrintDword
        call    PrintNewline

        test    dword [bp+pif_Eflags],EFLAGS_MASK_VM
        jz      .notv86
        mov     eax,[bp+pif_v86_ESP]
        mov     [bp+pif_ESP],eax
.notv86:
        push    bp
        mov     bp,sp
        add     bp,pif_SavedRegs-2 ; -2 for push of bp
        call    PrintRegs
        pop     bp
        test    dword [bp+pif_Eflags],EFLAGS_MASK_VM
        jnz      .v86
        call    PrintSregs
        jmp     .out
.v86:
        lea     di,[bp+pif_v86_SS]
        mov     si,.v86srtext
        mov     cx,5
.v86sregs:
        call    PrintString
        add     si,5
        mov     ax,[ss:di]
        add     di,4
        call    PrintWord
        loop    .v86sregs
        call    PrintNewline
.out:
        popad
        pop     ds

.halt:  hlt
        jmp     .halt
.msg:
        db "Unexpected protected mode interrupt #0x",0
.v86msg:
        db "Unexpected V86 interrupt #0x",0
.msg_errcode:
        db "Error code ",0
.v86srtext:
        db "SS=",0,0 ;Aligned
        db " ES=",0
        db " DS=",0
        db " ES=",0
        db " FS=",0
        db " GS=",0

ProtIDTR:
        dw      INT_HANDLER_COUNT*8-1
        dd      IDT_BASE

GDT_FLAG_MASK_DB EQU 1<<2 ; 0=16-bit, 1=32-bit
GDT_FLAG_MASK_G  EQU 1<<3 ; 0=1 byte limit, 1=4K limit

GDT_ACCESS_BIT_DPL      EQU 5
GDT_ACCESS_MASK_A       EQU 1<<0 ; Accessed
GDT_ACCESS_MASK_RW      EQU 1<<1 ; Read/Write (for code means read access allowed, for data segment writable)
GDT_ACCESS_MASK_DC      EQU 1<<2 ; Direction/Conforming
GDT_ACCESS_MASK_E       EQU 1<<3 ; Executable
GDT_ACCESS_MASK_S       EQU 1<<4 ; Set if code/data segment
GDT_ACCESS_MASK_DPL     EQU 3<<GDT_ACCESS_BIT_DPL
GDT_ACCESS_MASK_P       EQU 1<<7 ; Present

GDT_ACCESS_CODE         EQU GDT_ACCESS_MASK_RW|GDT_ACCESS_MASK_E|GDT_ACCESS_MASK_S|GDT_ACCESS_MASK_P
GDT_ACCESS_DATA         EQU GDT_ACCESS_MASK_RW|GDT_ACCESS_MASK_S|GDT_ACCESS_MASK_P

%macro GDT_ENTRY 4 ; base, limit, access, flags
        dw      ((%2)&0xffff)
        dw      ((%1)&0xffff)
        dw      (((%1)>>16)&0xff)|((%3)<<8)
        dw      (((%2)>>16)&0xf)|((%4)<<4)|(((%1)>>24)<<8)
%endmacro

ProtGDT:
        dd              0,0
        GDT_ENTRY       0,0xfffff,GDT_ACCESS_CODE,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G
        GDT_ENTRY       0,0xfffff,GDT_ACCESS_DATA,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G
        GDT_ENTRY       0,0xffff,GDT_ACCESS_CODE,0 ; 16-bit code
        GDT_ENTRY       0,0xffff,GDT_ACCESS_DATA,0 ; 16-bit data
        GDT_ENTRY       TSS_BASE,TSS_SIZE-1,GDT_ACCESS_MASK_P|0x9,0 ; TSS
ProtGDTEnd:

ProtGDTR:
        dw      ProtGDTEnd-ProtGDT-1
        dd      GDT_BASE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Display
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        %if CGA_DISPLAY

DisplayInit:
        push    ax
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Display off
        mov     dx,0x3d8
        xor     al,al
        out     dx,al

        mov     ax,0xb800
        mov     es,ax
        xor     di,di
        mov     ax,0x0700
        mov     cx,80*25
        rep     stosw

        mov     si,.mode3
        mov     dx,0x3d4
        xor     ah,ah
.init:
        mov     al,ah
        out     dx,al
        cs lodsb
        inc     dx
        out     dx,al
        dec     dx
        inc     ah
        cmp     ah,16
        jne     .init

        mov     dx,0x3d9
        xor     al,al
        out     dx,al

        mov     dx,0x03d8
        mov     al,1<<5|1<<3|1<<0 ; 80x25 text
        out     dx,al

        push    ds
        xor     ax,ax
        mov     ds,ax
        mov     word [DISPLAY_CURSOR_ADDR],0
        pop     ds

        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ax
        ret
.mode3:
        db 0x71,0x50,0x5A,0x0A,0x1F,0x06,0x19,0x1C
        db 0x02,0x07,0x09,0x09,0x00,0x00,0x00,0x00

DisplayChar:
        push    ax
        push    bx
        push    cx
        push    dx
        push    ds
        push    es
        mov     cl,al   ; cl = char
        mov     ch,7    ; cx = char and attr
        xor     bx,bx
        mov     ds,bx
        mov     dx,[DISPLAY_CURSOR_ADDR] ; dx = cursor pos
        mov     bx,0xb800
        mov     es,bx

        cmp     al,10
        je      .nl
        cmp     al,13
        je      .cr

        mov     al,80
        mul     dh
        add     al,dl
        adc     ah,0
        add     ax,ax
        mov     bx,ax
        mov     [es:bx],cx
        inc     dl
        cmp     dl,80
        jne     .done
        xor     dl,dl
.nl:
        inc     dh
        cmp     dh,24
        jne     .done
        dec     dh
        ;       Scroll
        push    ds
        push    es
        pop     ds
        push    si
        push    di
        cld
        xor     di,di
        mov     si,80*2
        mov     cx,80*24
        rep     movsw
        mov     ax,0x0700
        mov     cx,80
        rep     stosw
        pop     di
        pop     si
        pop     ds
        jmp     .done
.cr:
        xor     dl,dl
.done:
        mov     [DISPLAY_CURSOR_ADDR],dx
        pop     es
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

        %endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Serial port
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        %if SER_OUTPUT

SERIAL_PORT EQU 0x3f8
SERIAL_DATA EQU SERIAL_PORT
SERIAL_STATUS EQU SERIAL_PORT+5

SerialInit:
        ret

SerPutChar:
        push    dx
        push    ax
        mov     dx,SERIAL_STATUS
.wait:
        in      al,dx
        test    al,1<<5 ; Wait for LS_THRE
        jz      .wait
        pop     ax
        mov     dx,SERIAL_DATA
        out     dx,al
        pop     dx
        ret

        %endif ; SER_OUTPUT

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Output
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PrintChar:
        %if CGA_DISPLAY
        push    ax
        cmp     al,10
        jne     .display
        mov     al,13
        call    DisplayChar
        mov     al,10
.display:
        call    DisplayChar
        pop     ax
        %endif
        %if SER_OUTPUT
        call    SerPutChar
        %endif
        %if DEBUG_PORT
        out     DEBUG_PORT,al
        %endif
        ret

PrintRegs: ; bp=registers (PUSHAD style)
        pusha
        add     bp,32
        mov     si,.RegNames
        mov     cx,8
.print:
        call    PrintString
        add     si,5
        sub     bp,4
        mov     eax,[bp]
        call    PrintDword
        mov     al,' '
        mov     ah,cl
        and     ah,3
        cmp     ah,1
        jne     .sep
        mov     al,10
.sep:
        call    PrintChar
        loop    .print

        popa
        ret
.RegNames:
        db      "EAX=",0
        db      "ECX=",0
        db      "EDX=",0
        db      "EBX=",0
        db      "ESP=",0
        db      "EBP=",0
        db      "ESI=",0
        db      "EDI=",0

PrintSregs:
        push    ax
        push    si

        mov     si,.regs
        call    PrintString
        add     si,4
        mov     ax,cs
        call    PrintWord

        call    PrintString
        add     si,5
        mov     ax,ds
        call    PrintWord

        call    PrintString
        add     si,5
        mov     ax,es
        call    PrintWord

        call    PrintString
        add     si,5
        mov     ax,fs
        call    PrintWord

        call    PrintString
        add     si,5
        mov     ax,gs
        call    PrintWord

        call    PrintString
        add     si,5
        mov     ax,ss
        call    PrintWord

        call    PrintNewline
        pop     si
        pop     ax
        ret
.regs:
        db      "CS=",0
        db      " DS=",0
        db      " ES=",0
        db      " FS=",0
        db      " GS=",0
        db      " SS=",0

PrintNewline:
        push    ax
        mov     al,10
        call    PrintChar
        pop     ax
        ret

PrintString:
        push    ax
        push    si
.loop:
        cs lodsb
        test    al,al
        jz      .done
        call    PrintChar
        jmp     .loop
.done:
        pop     si
        pop     ax
        ret

PrintDword:
        push    eax
        shr     eax,16
        call    PrintWord
        pop     eax
PrintWord:
        push    ax
        mov     al,ah
        call    PrintByte
        pop     ax
PrintByte:
        push    ax
        shr     al,4
        call    PrintNibble
        pop     ax
PrintNibble:
        push    ax
        and     al,15
        add     al,'0'
        cmp     al,'9'
        jbe     .print
        add     al,'A'-'0'-10
.print:
        call    PrintChar
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Real mode tests
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

TestRealModeLimit:
        xor     ax,ax
        mov     ds,ax
        push    word [0xd*4]
        mov     word [0xd*4],.ok
        mov     edi,2*1024*1024+SCRATCH
        mov     byte [edi],42
        push    cs
        pop     ds
        mov     si,.FailMsg
        call    PrintString
        ;jmp     RModeFail; XXX: This check isn't done by QEMU/DosBox...
        pop     word [0xd*4]
        ret
.ok:
        add     sp,6
        pop     word [0xd*4]
        ret
.FailMsg:
        db      "Real mode limit check not done",10,0

TestUnrealmode:
        ; Unreal mode test
        xor     ax,ax
        mov     ds,ax
        mov     word [SCRATCH],0x5a5a
        lgdt    [cs:ProtGDTR]
        mov     eax,cr0
        or      al,1
        mov     cr0,eax
        mov     bx,GDT_SEL_DATA32
        mov     ds,bx
        mov     dx,0xABCD
        mov     edi,2*1024*1024+SCRATCH
        mov     word [edi],dx
        and     al,0xfe
        mov     cr0,eax
        CHECK_EQ word [edi],dx
        xor     ax,ax
        mov     ds,ax
        CHECK_EQ word [SCRATCH],0x5a5a
        CHECK_EQ word [edi],dx
        ret

TestTransitionCPL:
        push    cs
        call    .test
        ret
.test:
        ; Ensure we are executing with a CS that has low bits set
        mov     ax,cs
	or	ax,3
        push    ax
	push	word .next
        retf
        align 64
.next:
	times 64 nop
	mov	eax,cr0
	or	al,1
	mov	cr0,eax

	clts	; Privileged instruction, shouldn't cause #GP since CPL=0

	mov	eax,cr0
	and	al,0xfe
	mov	cr0,eax

        retf

TestRealmodeSTR:
        xor     ax,ax
        mov     ds,ax
        push    word [EXCEPTION_UD*4]
        mov     word [EXCEPTION_UD*4],.ok
        str     ax      ; Should cause #UD
        call    RModeFail
.ok:
        add     sp,6
        pop     word [EXCEPTION_GP*4]
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; V86 mode tests
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

struc V86_StackFrame
        v86_EIP         resd    1
        v86_CS          resd    1
        v86_Eflags      resd    1
        v86_ESP         resd    1
        v86_SS          resd    1
        v86_ES          resd    1
        v86_DS          resd    1
        v86_FS          resd    1
        v86_GS          resd    1
        v86_sizeof      resb 0
endstruc


        bits 32
V86Call:
        mov     word [TSS_BASE+TSS_SS0],GDT_SEL_DATA32
        mov     [TSS_BASE+TSS_ESP0],esp

        push    V86Exit
        sub     esp,v86_sizeof
        mov     dword [esp+v86_EIP],ebp
        GET_REAL_SEG ebp
        mov     dword [esp+v86_CS],ebp
        mov     dword [esp+v86_Eflags],EFLAGS_MASK_VM
        xor     ebp,ebp
        mov     [esp+v86_ESP],esp
        add     dword [esp+v86_ESP],v86_sizeof
        mov     [esp+v86_SS],ebp
        mov     [esp+v86_ES],ebp
        mov     [esp+v86_DS],ebp
        mov     [esp+v86_FS],ebp
        mov     [esp+v86_GS],ebp
        iretd

V86Exit:
        bits    16
        SET_HANDLER EXCEPTION_GP,.exitv86
        int     0x10
        call    RModeFail
        bits    32
.exitv86:
        add     esp,v86_sizeof+4 ; +4 for the error code
        mov     ax,GDT_SEL_DATA32
        mov     ds,ax
        RESTORE_HANDLER EXCEPTION_GP
        ret

TestV86:
        ; XXX: DosBox doesn't like SS=0 in TSS
        ; Of course it's a bad idea, but unlikely HW checks it on iret
        mov     word [TSS_BASE+TSS_SS0],GDT_SEL_DATA32
        mov     [TSS_BASE+TSS_ESP0],esp

        mov     eax,esp
        push    0x65
        push    0xf5
        push    0xd5
        push    0xe5
        push    0
        push    eax
        push    dword EFLAGS_MASK_VM
        GET_REAL_SEG eax
        push    eax
        push    dword V86start

        iretd

        bits 16

V86start:
        ; Check segment registers
        mov     bx,cs
        CHECK_EQ ax,bx   ; pushed CS
        mov     bx,ds
        CHECK_EQ bx,0xd5
        mov     bx,es
        CHECK_EQ bx,0xe5
        mov     bx,fs
        CHECK_EQ bx,0xf5
        mov     bx,gs
        CHECK_EQ bx,0x65
        mov     bx,ss
        CHECK_EQ bx,0

        push    cs
        pop     ds
        mov     si,.v86_hello
        call    PrintString

        POST    5

        or      byte [cs:0],0 ; Write through CS


        ;
        ; Check #GP for varoius instructions
        ;

        ; First with complete state checked
        SET_HANDLER EXCEPTION_GP,.gp1
        mov     eax,cs
        mov     ebx,ds
.cli:
        cli
.gp1:
        bits 32
        CHECK_EQ dword [esp+0],0 ; Error code
        CHECK_EQ dword [esp+4],.cli ; EIP
        CHECK_EQ dword [esp+8],eax  ; CS
        test    dword [esp+12],EFLAGS_MASK_VM ; Eflags
        CHECK_CC nz
        CHECK_EQ dword [esp+20],0 ; SS
        CHECK_EQ dword [esp+24],0xe5 ; ES
        CHECK_EQ dword [esp+28],ebx ; DS
        CHECK_EQ dword [esp+32],0xf5 ; FS
        CHECK_EQ dword [esp+36],0x65 ; GS
        add     esp,4 ; pop error code
        mov     dword [esp],.cont
        iretd
        bits 16
.cont:
        mov     eax,gs
        CHECK_EQ eax,0x65

        SET_HANDLER EXCEPTION_GP,.gp2
        jmp     .gp2test
.gp2:
        bits 32
        CHECK_EQ dword [esp],0      ; Error code
        add     esp,4
        CHECK_EQ [esp],eax          ; EIP
        mov     [esp],ebx               ; Advance past instruction(s)
        iret
        bits 16

.gp2test:

        %macro GP2_TEST 1+
        mov     eax,%%instruction
        mov     ebx,%%next
%%instruction:
        %1
        call    RModeFail
%%next:
        %endmacro

        GP2_TEST sti
        GP2_TEST cli
        GP2_TEST pushf
        GP2_TEST popf
        call    .x ; push return address
.x:
        push    word 0
        push    cs
        push    RModeFail
        GP2_TEST iret
        add     sp,8

        GP2_TEST int 0x10
        mov     dx,MAX_IO_PORT
        xor     al,al
        out     dx,al   ; Allowed
        ; TODO: Check access to IO ports > MAX_IO_PORT (Should #GP)
        ; TODO: Check access to IO port where bit is 1 in IO bitmap
        ; TODO: Try again with IOPL=3 (should succeed)
        ;inc     dx
        ;GP2_TEST out dx,al ; beyond permission bitmap limit
;
;        mov     dx,0x21
;        mov     al,0xff
;        out     dx,al   ; Allowed
;
;        xor     bx,bx
;        mov     ds,bx
;        or      byte [TSS_BASE+TSS_IO_BITMAP_START+0x21/8],1<<(0x21&7)
;
;        GP2_TEST out dx,al


        ; TODO: When is LOCK not allowed / handled specially?
        xor     ax,ax
        mov     ds,ax
        lock add byte [SCRATCH],1

        SET_HANDLER EXCEPTION_GP,.set_iopl
        jmp     .cont2
.set_iopl:
        bits    32
        add     esp,4
        mov     word [esp+8],3<<EFLAGS_BIT_IOPL
        mov     dword [esp],.cont3
        iretd
        bits    16
.cont2:
        cli
        call    RModeFail
.cont3:

        RESTORE_HANDLER EXCEPTION_GP

        ; now these are allowed
        sti
        cli
        pushf
        popf
        push    0
        push    cs
        push    word .cont4
        iret
        call    RModeFail
.cont4:
        cli     ; still allowed despite above flag change above

        push    word 0
        popf
        cli     ; still doesn't change IOPL

        ;
        ; interrupts with DPL=3
        ;

        ;TODO: This needs to be handled differently, selector can't be normal one

;        xor     ax,ax
;        mov     ds,ax
;        mov     byte [IDT_BASE+0xf*8+5],0xe6 ; 16-bit interrupt gate DPL=3
;        SET_HANDLER 0xf,.intF
;        jmp     .afterintf
;.intF:
;        ;mov     bp,sp
;        ;CHECK_EQ word [bp],.afterintf+2 ; IP is as expected
;;        mov     ax,cs
;;        CHECK_EQ [bp+2],ax
;        iret
;.afterintf:
;        xchg    bx,bx
;        int     0xf
;
;        xor     ax,ax
;        mov     ds,ax
;        mov     byte [IDT_BASE+0xf*8+5],0x8e ; Restore int gate to 32-bit DPL=0
;        RESTORE_HANDLER 0xf

        ;
        ; Limit check + unreal mode
        ;

        xor     ax,ax
        mov     ds,ax
        mov     word [SCRATCH],0xabcd
        mov     edi,2*1024*1024+SCRATCH
        SET_HANDLER EXCEPTION_GP,.set_unreal
        mov     word [edi],0x1234
        call    RModeFail
        bits 32
.set_unreal:
        add     esp,4
        and     word [esp+8],~(3<<EFLAGS_BIT_IOPL) ;IOPL back to 0
        mov     dword [esp],.cont5
        mov     ax,GDT_SEL_DATA32
        mov     ds,ax
        iretd
.expect_fail:
        add     esp,4
        mov     dword [esp],.cont6
        iretd
        bits 16
.cont5:
        SET_HANDLER EXCEPTION_GP,.expect_fail

        CHECK_EQ word [edi],0x1234 ; DS is really reloaded with limit=0xffff
        ; Not checked by DosBox/QEMU..
        push    ds
        push    cs
        pop     ds
        mov     si,.v86_unreal_possible
        call PrintString
        pop     ds
.cont6:
        RESTORE_HANDLER EXCEPTION_GP
        CHECK_EQ word [SCRATCH],0xabcd

        ; retf limit check
        push    cs
        push    .cont7
        retf
.cont7:

        ; TODO: Test #UD instructions (e.g. "str ax")

        push    cs
        pop     ds
        mov     si,.v86_exit
        call    PrintString

        jmp     V86Exit

.v86_hello:
        db      "In V86 mode!",10,0
.v86_exit:
        db      "V86 tests done!",10,0
.v86_unreal_possible:
        db      "Unreal mode should not be possible in V86!",10,0
;;;;;;;;;;;;;;;;;;;;;;;;;;;

        times ROM_SIZE-16-($-$$) hlt

        jmp 0x10000-ROM_SIZE/16:BiosEntry
        times 16-5 db '?'
