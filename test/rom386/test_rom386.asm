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
; TODO: Should a RMW operation set the W bit? Qemu/bochs do not agree.
; TODO: Test "NT" flag + Task linking/switching (80386 PRM section 7.6)
; TODO: Which flags are allowed to be pushed/popped
; TODO: INT allowed to interrupt level 3...
; TODO: Verify error code for interrupt not present/not an interrupt gate
; TODO: Test 32-bit call gate from 16-bit mode (and vice versa)
; TODO: Test LDT
; TODO: Need to enable A20 before test (or at least ensure it's on)

        cpu 386
        bits 16
        org 0

%include "gdt.inc"

ROM_SIZE EQU 0x10000

INT_HANDLER_COUNT EQU 32
INT_HANDLER_SIZE EQU 4

MAX_IO_PORT EQU 0x500

TSS_SS0  EQU 0x08
TSS_ESP0 EQU 0x04
TSS_IOPB EQU 0x66
TSS_BASE_SIZE EQU 0x68
TSS_SIZE EQU TSS_BASE_SIZE+4*((MAX_IO_PORT+31)/32)
TSS_IO_BITMAP_START EQU TSS_BASE_SIZE

TSS16_SP0 EQU 0x02
TSS16_SS0 EQU 0x04
TSS16_SIZE EQU 0x2C

EXCEPTION_UD EQU 6
EXCEPTION_NP EQU 11
EXCEPTION_GP EQU 13
EXCEPTION_PF EQU 14

EFLAGS_MASK_ALWAYS0 EQU 1<<15|1<<5|1<<3
EFLAGS_MASK_ALWAYS1 EQU 1<<1
EFLAGS_MASK_IF EQU 1<<9
EFLAGS_BIT_IOPL EQU 12
EFLAGS_MASK_TF EQU 1<<8
EFLAGS_MASK_VM EQU 1<<17

HAS_ERROR_CODE_MASK EQU 1<<8|1<<10|1<<11|1<<12|1<<13|1<<14|1<<17|1<<30

DISPLAY_CURSOR_ADDR EQU 0x400
IDT_BASE EQU 0x1000
EMU_ID EQU 0x1800
GDT_BASE EQU 0x2000
LDT_BASE EQU 0x2400
LDT_SIZE EQU 0x60
TSS_BASE EQU 0x2800
TSS16_BASE EQU TSS_BASE+TSS_SIZE
SCRATCH EQU 0x3000
STACK2_TOP EQU 0x7000
STACK1_TOP EQU 0x8000


PAGE_SIZE       EQU 4096

PT_MASK_P       EQU 1<<0        ; Present
PT_MASK_W       EQU 1<<1        ; Writable
PT_MASK_U       EQU 1<<2        ; User

PAGING_BASE     EQU 0x10000

%assign PAGING_NEXT_PAGE PAGING_BASE
%macro ALLOC_PAGES 1-2 1
%1 EQU PAGING_NEXT_PAGE
%assign PAGING_NEXT_PAGE PAGING_NEXT_PAGE+(PAGE_SIZE*%2)
%endmacro

ALLOC_PAGES PDT_BASE
ALLOC_PAGES PD000               ; Identity map  00000000-00400000 (only mapped to 00100000)
ALLOC_PAGES PD800               ; Test map      80000000-80400000
ALLOC_PAGES TEST_PAGE0
ALLOC_PAGES TEST_PAGE1
ALLOC_PAGES TEST_PAGE2

TEST_PAGE0_VIRT EQU 0x80000000+0*PAGE_SIZE
TEST_PAGE1_VIRT EQU 0x80000000+1*PAGE_SIZE
TEST_PAGE2_VIRT EQU 0x80000000+2*PAGE_SIZE

EMU_ID_OTHER EQU 0
EMU_ID_DOSBOX EQU 1
EMU_ID_QEMU EQU 2

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
        and     eax,0x7fffffe ; Not PE and no paging
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

        mov     bl,EMU_ID_DOSBOX
        ; For the sake of DosBox use iret to clear NT flag ?!?!?!
        ;jmp     COM_SEG:BiosEntry
        push    0
        push    COM_SEG
        push    Entry2
        iret

BiosEntry:
        mov     bl,EMU_ID_OTHER
        xor     ax,ax
        mov     ds,ax
        mov     word [EXCEPTION_UD*4],Entry2
        mov     [EXCEPTION_UD*4+2],cs
        mov     eax,0x40000000
        xor     ebx,ebx
        xor     ecx,ecx
        xor     edx,edx
        db      0x0F, 0xA2 ; CPUID
        cmp     ebx,'TCGT'
        je      .qemu
        cmp     ebx,'KVMK'
        jne     .other
.qemu:
        mov     bl,EMU_ID_QEMU
        jmp     Entry2
.other:
        mov     bl,EMU_ID_OTHER
Entry2:
	cli
        cld
	xor	ax,ax
	mov	ss,ax
        mov     ax,STACK1_TOP
        mov     sp,ax

        mov     [ss:EMU_ID],bl

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
        mov     al,[ss:EMU_ID]
        call    PrintByte
        mov     al,10
        call    PrintChar

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

        ; Make a not present handler
        xor     eax,eax
        stosd
        stosd

        mov     di,GDT_BASE
        push    cs
        pop     ds
        mov     si,ProtGDT
        mov     cx,(ProtGDTEnd-ProtGDT)/4
        rep     movsd

        ; Fix offset of "ORIG16", "ORIG32" and "USER16" descriptors
        mov     eax,cs
        shl     eax,4
        or      [es:GDT_BASE+GDT_SEL_ORIG16+2],eax
        or      [es:GDT_BASE+GDT_SEL_USER16+2],eax
        or      [es:GDT_BASE+GDT_SEL_ORIG32+2],eax

        ; Init LDT
        mov     di,LDT_BASE
        xor     eax,eax
        mov     ecx,LDT_SIZE/4
        rep     stosd

        ; Clear TSS
        mov     edi,TSS_BASE
        mov     ecx,TSS_SIZE/4
        xor     eax,eax
        rep     stosd
        ; Init IOPB
        mov     word [es:TSS_BASE+TSS_IOPB],TSS_IO_BITMAP_START

        POST    0

        call    TestRealModeLimit
        call    TestUnrealmode
        call    TestTransitionCPL
        call    TestRealModeSTR
        call    TestRealModeFlags

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

        or      ax,4
        lmsw    ax
        mov     ebx,cr0
        CHECK_EQ ax,bx

        ; Task is marked available
        CHECK_EQ byte [GDT_BASE+GDT_SEL_TSS+5],0x89

        ; LTR/STR
        xor     eax,eax
        mov     ax,GDT_SEL_TSS
        ltr     ax
        str     ebx
        CHECK_EQ ax,bx

        ; Task is marked busy
        mov     bl,[GDT_BASE+GDT_SEL_TSS+5]
        CHECK_EQ bl,0x8B

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

        ; SIDT
        mov     edi,SCRATCH
        sidt    [edi]
        mov     ax,GDT_SEL_ORIG16
        mov     es,ax
        mov     ax,[es:ProtIDTR]
        CHECK_EQ [edi],ax
        mov     eax,[es:ProtIDTR+2]
        CHECK_EQ [edi+2],eax

        ; LLDT / SGDT
        mov     ax,GDT_SEL_LDT
        lldt    ax
        sldt    bx
        CHECK_EQ bx,ax
        xor     ax,ax
        lldt    ax
        sldt    bx
        CHECK_EQ bx,ax

        ; IRET in protected mode
        xor     eax,eax
        push    eax
        push    dword GDT_SEL_CODE32
        GET_ADDR_REL eax,.iret
        push    eax
        iret
.iret:

        ; LSL
        mov     eax,0xf000 ; over limit
        mov     edx,0xabcd
        lsl     edx,eax
        CHECK_CC nz
        CHECK_EQ edx,0xabcd
        mov     eax,GDT_SEL_CODE32|2 ; RPL > DPL
        lsl     edx,eax
        CHECK_CC nz
        CHECK_EQ edx,0xabcd
        mov     eax,GDT_SEL_CODE32
        lsl     edx,eax
        CHECK_CC z
        CHECK_EQ edx,0x67fff<<12|0xfff

        xor     eax,eax
        push    dword GDT_SEL_CODE32
        lsl     eax,[esp]
        CHECK_CC z
        add     esp,4
        CHECK_EQ eax,0x67fff<<12|0xfff


        ; LAR
        mov     eax,0xf000 ; over limit
        mov     edx,0xabcd
        lar     edx,eax
        CHECK_CC nz
        CHECK_EQ edx,0xabcd
        mov     eax,GDT_SEL_DATA32
        lar     edx,eax
        CHECK_CC z
        mov     ecx,[GDT_BASE+GDT_SEL_DATA32+4]
        ; bits 19:16 are undefined
        and     ecx,0x00f0ff00
        and     edx,0xfff0ffff
        CHECK_EQ edx,ecx

        POST    4
        call    TestV86

        mov     si,.msg
        CALL_V86 .printmsg

        POST    6

        call    GDT_SEL_ORIG16:TestTask16

        POST    7

        call    GDT_SEL_ORIG32:TestGP32

        POST    8

        ; Set max segment limit for GDT_SEL_CODE32
        mov     ax,GDT_SEL_DATA32
        mov     ds,ax
        mov     word [GDT_BASE+GDT_SEL_CODE32],0xffff
        or      byte [GDT_BASE+GDT_SEL_CODE32+6],0xf

        GET_LINEAR_BASE eax
        mov     ebx,.pgdone
        add     ebx,eax
        push    ebx ; return address
        push    dword GDT_SEL_CODE32
        add     eax,TestPaging
        push    eax
        retf
.pgdone:

        POST    0xff
        mov     si,.alltests
        CALL_V86 .printmsg

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
        ;DEBUG_BREAK
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
        db "Starting test",10,"EMU ID: ",0
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
        dw      (INT_HANDLER_COUNT+1)*8-1
        dd      IDT_BASE

ProtGDT:
        GDT_TABLE
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
        cmp     byte [EMU_ID],EMU_ID_OTHER ; DosBox/qemu don't do this check
        je      RModeFail
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

TestRealModeSTR:
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

TestRealModeFlags:
        ; FreeDOS JEMMEX 386 test:
        mov     ax,0x7000
        push    ax
        popf
        pushf
        pop     bx
        or      al,2 ; bit 1 is always set
        cmp     ax,bx
        jne     RModeFail

        push    0xffff-EFLAGS_MASK_TF
        popf
        pushf
        pop     ax
        cmp     ax,~(EFLAGS_MASK_ALWAYS0|EFLAGS_MASK_TF)
        jne     RModeFail

        ; For DosBOX make sure NT is cleared..
        xor     ax,ax
        push    ax
        popf
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

        mov     word [STACK2_TOP-2],V86Exit ; Put exit function on V86 stack
        sub     esp,v86_sizeof
        mov     dword [esp+v86_EIP],ebp
        GET_REAL_SEG ebp
        mov     dword [esp+v86_CS],ebp
        mov     dword [esp+v86_Eflags],EFLAGS_MASK_VM
        xor     ebp,ebp
        mov     dword [esp+v86_ESP],STACK2_TOP-2
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

        push    0x65
        push    0xf5
        push    0xd5
        push    0xe5
        push    0
        push    STACK2_TOP
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

        ; Task marked busy
        xor     ax,ax
        mov     ds,ax
        mov     al,[GDT_BASE+GDT_SEL_TSS+5]
        CHECK_EQ al,0x8B

        ;
        ; Check #GP for various instructions
        ;

        ; First with complete state checked
        SET_HANDLER EXCEPTION_GP,.gp1
        mov     eax,cs
        mov     ebx,ds
.cli:
        cli
.gp1:
        bits 32

        push    ax
        mov     ax,cs
        CHECK_EQ ax,GDT_SEL_CODE32
        mov     ax,ds
        CHECK_EQ ax,0
        mov     ax,es
        CHECK_EQ ax,0
        mov     ax,fs
        CHECK_EQ ax,0
        mov     ax,gs
        CHECK_EQ ax,0
        mov     ax,ss
        CHECK_EQ ax,GDT_SEL_DATA32
        pop     ax

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
        GP2_TEST lmsw ax
        call    .x ; push return address
.x:
        push    word 0
        push    cs
        push    RModeFail
        GP2_TEST iret
        add     sp,8

        GP2_TEST int 0x10

        mov     dx,MAX_IO_PORT-1
        xor     al,al
        out     dx,al   ; Allowed
        inc     dx
        GP2_TEST out dx,al ; beyond permission bitmap limit

        xor     bx,bx
        mov     ds,bx
        or      byte [TSS_BASE+TSS_IO_BITMAP_START+(MAX_IO_PORT-1)/8],1<<((MAX_IO_PORT-1)&7)

        mov     dx,MAX_IO_PORT-1 ; Disallowed in IO bitmap
        GP2_TEST out dx,al

        dec     dx
        GP2_TEST out dx,ax ; Second part of port disallowed

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
        SET_HANDLER EXCEPTION_GP,.gp2

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
        pushf
        pop     ax
        CHECK_EQ ax,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL

        push    word ~EFLAGS_MASK_TF
        popf
        pushf
        pop     ax
        CHECK_EQ ax,~(EFLAGS_MASK_ALWAYS0|EFLAGS_MASK_TF)

        push    word 0
        popf
        pushf
        pop     ax
        CHECK_EQ ax,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL ; IOPL not changed

        mov     eax,0xabcdabcd
        mov     ebx,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL
        xor     ecx,ecx
        inc     cl
        pushfd
        pop     eax
        CHECK_EQ eax,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL

        push    dword 0
        popfd
        pushfd
        pop     eax
        nop ; FIXME why does this make a difference for qemu/dosbox?
        CHECK_EQ eax,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL

        mov     dx,MAX_IO_PORT-2
        out     dx,al ; Allowed
        inc     dx
        GP2_TEST out dx,al   ; Still checked even when IOPL=3
        inc     dx
        GP2_TEST out dx,al ; Still blocked

        push    word 0
        popf
        cli     ; still doesn't change IOPL

        ; Interrupt handler not present
        ; TOOD: Shouldn't error code be +2?
        SET_HANDLER EXCEPTION_GP,.not_present_int
        int     INT_HANDLER_COUNT
        call    RModeFail
.not_present_int:
        bits 32
        cmp     dword [esp],INT_HANDLER_COUNT*8 ; Error code
        add     esp,4
        mov     dword [esp],.not_present_int_done
        iretd
        bits    16
.not_present_int_done:

        SET_HANDLER EXCEPTION_GP,.wrong_dpl_int
        int     0x10
        call    RModeFail
.wrong_dpl_int:
        bits 32
        cmp     dword [esp],INT_HANDLER_COUNT*8 ; Error code
        add     esp,4
        mov     dword [esp],.wrong_dpl_int_done
        iretd
        bits    16
.wrong_dpl_int_done:
        RESTORE_HANDLER EXCEPTION_GP


        ;
        ; Interrupts with DPL=3 and CODE16
        ;

        xor     ax,ax
        mov     es,ax
        mov     di,IDT_BASE+0xf*8
        push    dword [es:di]
        push    dword [es:di+4]

        mov     ax,.int0F               ; offset low
        stosw
        mov     ax,GDT_SEL_ORIG16       ; selector
        stosw
        mov     ax,0xee00               ; 32-bit interrupt gate, DPL=3
        stosw
        xor     ax,ax                   ; offset high
        stosw

        push    word 0x1234
        pop     ds
        push    word 0x5678
        pop     es
        push    word 0x9abc
        pop     fs
        push    word 0xdef0
        pop     gs

        jmp     .after_int
.int0F:
        push    ax
        mov     ax,cs
        CHECK_EQ ax,GDT_SEL_ORIG16
        mov     ax,ds
        CHECK_EQ ax,0
        mov     ax,es
        CHECK_EQ ax,0
        mov     ax,fs
        CHECK_EQ ax,0
        mov     ax,gs
        CHECK_EQ ax,0
        mov     ax,ss
        CHECK_EQ ax,GDT_SEL_DATA32
        pop     ax
        CHECK_EQ dword [esp+0x00],.after_int+2
        test    dword [esp+0x08],EFLAGS_MASK_VM
        CHECK_CC nz
        CHECK_EQ dword [esp+0x14],0x5678
        CHECK_EQ dword [esp+0x18],0x1234
        CHECK_EQ dword [esp+0x1C],0x9abc
        CHECK_EQ dword [esp+0x20],0xdef0
        iretd
.after_int:
        int     0xf

        mov     ax,ds
        CHECK_EQ ax,0x1234
        mov     ax,es
        CHECK_EQ ax,0x5678
        mov     ax,fs
        CHECK_EQ ax,0x9abc
        mov     ax,gs
        CHECK_EQ ax,0xdef0

        ; Restore handler
        xor    ax,ax
        mov    ds,ax
        pop    dword [IDT_BASE+0xf*8+4]
        pop    dword [IDT_BASE+0xf*8]

        ;
        ; Limit check + unreal mode
        ; Not checked by DosBox/QEMU..

        xor     ax,ax
        mov     ds,ax
        mov     word [SCRATCH],0xabcd
        mov     edi,2*1024*1024+SCRATCH
        SET_HANDLER EXCEPTION_GP,.set_unreal
        mov     word [edi],0x1234
        cmp     byte [EMU_ID],EMU_ID_OTHER
        jne     .cont5
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
        RESTORE_HANDLER EXCEPTION_GP
        push    cs
        pop     ds
        mov     si,.v86_unreal_possible
        call PrintString
        xor     ax,ax
        mov     ds,ax
        mov     al,[EMU_ID]
        cmp     byte [EMU_ID],EMU_ID_OTHER
        jne     .cont6
        call    RModeFail
.cont6:
        RESTORE_HANDLER EXCEPTION_GP
        CHECK_EQ word [SCRATCH],0xabcd

        ; retf limit check
        push    cs
        push    .cont7
        retf
.cont7:

        %macro V86_UD_TEST 1+
        mov     eax,%%instruction
        mov     ebx,%%next
%%instruction:
        %1
        call    RModeFail
%%next:
        %endmacro

        SET_HANDLER EXCEPTION_UD,.udhandler
        jmp     .cont8
        bits 32
.udhandler:
        CHECK_EQ [esp],eax
        mov     [esp],ebx
        iretd
        bits 16
.cont8:
        ; TODO: Check ALL!
        V86_UD_TEST str ax
        V86_UD_TEST arpl [bx+si],ax

        RESTORE_HANDLER EXCEPTION_UD
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

        bits    16
TestTask16:
        ; Setup 16-bit interrupt gate for int 0x13
        mov     ax,GDT_SEL_DATA16
        mov     es,ax
        mov     ds,ax

        ; Save old interrupts
        push    dword [IDT_BASE+EXCEPTION_GP*8]
        push    dword [IDT_BASE+EXCEPTION_GP*8+4]
        push    dword [IDT_BASE+EXCEPTION_NP*8]
        push    dword [IDT_BASE+EXCEPTION_NP*8+4]
        push    dword [IDT_BASE+0x13*8]
        push    dword [IDT_BASE+0x13*8+4]
        push    dword [IDT_BASE+0x14*8]
        push    dword [IDT_BASE+0x14*8+4]

        mov     di,IDT_BASE+0x13*8
        mov     ebx,TestInt13
        mov     ax,bx
        stosw   ; offset low
        mov     ax,GDT_SEL_ORIG16 ; segment selector
        stosw
        mov     ax,0xE600 ; type (present, DPL=3, 16-bit interrupt gate)
        stosw
        mov     eax,ebx
        shr     eax,16
        stosw

        GET_LINEAR_BASE ebx
        add     ebx,TestGP_16_32
        mov     di,IDT_BASE+EXCEPTION_GP*8
        mov     ax,bx
        stosw   ; offset low
        mov     ax,GDT_SEL_CODE32 ; segment selector
        stosw
        mov     ax,0x8E00 ; type (present, DPL=0, 32-bit interrupt gate)
        stosw
        mov     eax,ebx
        shr     eax,16
        stosw

        mov     word [TSS16_BASE+TSS16_SS0],GDT_SEL_DATA16
        mov     [TSS16_BASE+TSS16_SP0],sp

        mov     ax,GDT_SEL_TSS16
        ltr     ax

        push    word GDT_SEL_UD16|3
        push    word STACK2_TOP
        push    word 0
        push    word GDT_SEL_USER16|3
        push    word Task16
        iret
ExitTestTask16:
        mov     ax,GDT_SEL_DATA32
        mov     ds,ax   ; reload DS

        ; Restore interrupts
        pop     dword [IDT_BASE+0x14*8+4]
        pop     dword [IDT_BASE+0x14*8]
        pop     dword [IDT_BASE+0x13*8+4]
        pop     dword [IDT_BASE+0x13*8]
        pop     dword [IDT_BASE+EXCEPTION_NP*8+4]
        pop     dword [IDT_BASE+EXCEPTION_NP*8]
        pop     dword [IDT_BASE+EXCEPTION_GP*8+4]
        pop     dword [IDT_BASE+EXCEPTION_GP*8]

        mov     ax,GDT_SEL_TSS
        and     byte [GDT_BASE+GDT_SEL_TSS+5],0xfd ; clear BUSY flag
        ltr     ax      ; reload task
        retfd

TI13_TEST1      EQU 0x1234
TI13_EXIT       EQU 0x5ac3
TI13_SETIOPL    EQU 0xb451
TI13_FAIL       EQU 0xcd12

%macro CHECK16_CC 1
        j%1     %%ok
        mov     bp,TI13_FAIL
        int     0x13
%%ok:
%endmacro

%macro CHECK16_EQ 2
        cmp     %1,%2
        CHECK16_CC e
%endmacro

        bits    32
TestGP_16_32:
        add     esp,4 ; pop error code
        cmp     [esp],ebx
        jne     .fail
        mov     [esp],eax ; continuation address
        cmp     word [esp+4],GDT_SEL_USER16|3
        jne     .fail
        cmp     dword [esp+12],STACK2_TOP ; ESP
        cmp     word [esp+16],GDT_SEL_UD16|3 ; SS
        jne     .fail
        iret
.fail:
        add     word [esp],5 ; adjust return address for printing
        mov     bp,[esp+4]
        mov     [esp+2],bp ; CS
        jmp     PModeFail
        bits    16

TestNP_16_16:
        CHECK_EQ si,EXCEPTION_NP
        jmp     Test16_16_Common
TestGP_16_16:
        CHECK_EQ si,EXCEPTION_GP
Test16_16_Common:
        mov     bp,sp
        CHECK16_EQ [bp],cx ; error code
        CHECK16_EQ [bp+2],bx ; IP
        CHECK16_EQ word [bp+4],GDT_SEL_USER16|3 ; CS
        CHECK16_EQ word [bp+8],dx ; SP
        CHECK16_EQ word [bp+10],GDT_SEL_UD16|3 ; SS
        mov     [bp+2],ax
        add     sp,2
        iret

TestInt13:
        cmp     bp,TI13_TEST1
        je      .test1
        cmp     bp,TI13_EXIT
        je      .exit
        cmp     bp,TI13_SETIOPL
        je      .setiopl
        push    word GDT_SEL_DATA16
        pop     ds
        push    dword 0
        push    dword GDT_SEL_CODE32
        GET_LINEAR_BASE ebp
        add     ebp,PModeFail
        push    ebp
        iretd
.exit:
        add     sp,10   ; pop interrupt stack
        jmp     ExitTestTask16
.test1:
        ; Check stack
        mov     bp,sp
        CHECK16_EQ [bp+0],ax
        CHECK16_EQ word [bp+2],GDT_SEL_USER16|3

        ; And our segment registers
        mov     bp,cs
        CHECK16_EQ bp,GDT_SEL_ORIG16
        mov     bp,ss
        CHECK16_EQ bp,GDT_SEL_DATA16
        iret
.setiopl:
        and     word [esp+4],~(3<<EFLAGS_BIT_IOPL)
        shl     ax,EFLAGS_BIT_IOPL
        or      [esp+4],ax
        not     bp
        iret

Fail16:
        mov     bp,TI13_FAIL
        int     0x13
        jmp     Fail16

Call16Entry:
        mov     bp,sp
        cmp     [bp+0],ax       ; IP
        CHECK16_EQ word [bp+2],GDT_SEL_USER16|3 ; CS
        CHECK16_EQ word [bp+4],0xabcd ; param 1
        CHECK16_EQ word [bp+6],0x1234 ; param 2
        CHECK16_EQ word [bp+8],STACK2_TOP-4 ; SP
        CHECK16_EQ word [bp+10],GDT_SEL_UD16|3 ; SS
        mov     bp,cs
        CHECK16_EQ bp,GDT_SEL_ORIG16
        mov     bp,ss
        CHECK16_EQ bp,GDT_SEL_DATA16
        mov     ds,bp ; A selector that's not available for user mode
        retf 4

Task16:
        mov     ax,.cont1
        mov     bp,TI13_TEST1
        int     0x13
.cont1:

        ;
        ; Test some #GP faults (through 32-bit interrupt gate)
        ;
%macro TEST_GP16_32 1+
        mov     eax,%%cont
        mov     ebx,%%inst
%%inst:
        %1
        jmp     Fail16
%%cont:
%endmacro

        TEST_GP16_32 cli
        TEST_GP16_32 sti

        ; Set IOPL=3 and check cli/sti again
        mov     bp,TI13_SETIOPL
        mov     al,3
        int     0x13
        cli
        sti

        push    word 0
        popf
        pushf
        pop     ax
        CHECK_EQ ax,EFLAGS_MASK_ALWAYS1|3<<EFLAGS_BIT_IOPL ; IOPL not changed


        push    word ~EFLAGS_MASK_TF
        popf
        pushf
        pop     ax
        mov     bx,~(EFLAGS_MASK_ALWAYS0|EFLAGS_MASK_TF)
        CHECK16_EQ ax,bx ; IOPL not changed, IF is changed

        ;
        ; Test 16/32-bit interrupt/trap gate behavior for IF
        ;
        jmp     .test_if
.get_flags:
        ; N.B. code must work in both 16- and 32-bit mode!
        pushf
        pop     ax
        iret
.test_if:
        mov     bx,GDT_SEL_UD16|3
        mov     ds,bx
        lea     bx,[IDT_BASE+0x14*8]
        mov     word [bx],.get_flags
        mov     word [bx+2],GDT_SEL_ORIG16
        mov     word [bx+4],0xE700 ; 16-bit trap gate, DPL=3
        mov     word [bx+6],0

        sti
        mov     eax,0xABCDABCD
        int     0x14
        test    ax,EFLAGS_MASK_IF
        CHECK16_CC nz ; IF not cleared
        shr     eax,16
        CHECK_EQ ax,0xABCD ; Really executed in 16-bit mode

        mov     byte [bx+5],0xE6 ; 16-bit interrupt gate, DPL=3
        mov     eax,0xABCDABCD
        int     0x14
        test    ax,EFLAGS_MASK_IF
        CHECK16_CC z    ; IF cleared (in handler)
        shr     eax,16
        CHECK_EQ ax,0xABCD ; Really executed in 16-bit mode

        cmp     byte [EMU_ID],EMU_ID_DOSBOX ; DosBox doesn't like these?
        je      .dbskip1

        mov     word [bx+2],GDT_SEL_ORIG32 ; 32-bit selector
        mov     byte [bx+5],0xEE ; 32-bit interrupt gate, DPL=3
        mov     eax,0xABCDABCD
        int     0x14
        test    ax,EFLAGS_MASK_IF
        CHECK16_CC z    ; IF cleared (in handler)
        shr     eax,16
        cmp     ax,0xABCD ; Really executed in 32-bit mode
        CHECK16_CC nz

        mov     byte [bx+5],0xEF ; 32-bit trap gate, DPL=3
        mov     eax,0xABCDABCD
        int     0x14
        test    ax,EFLAGS_MASK_IF
        CHECK16_CC nz    ; IF not cleared
        shr     eax,16
        cmp     ax,0xABCD ; Really executed in 32-bit mode
        CHECK16_CC nz

.dbskip1:

        ; IF cleared from here on
        cli

        ; Restore IOPL=0
        mov     bp,TI13_SETIOPL
        xor     al,al
        int     0x13

        ; 16-bit call gate with parameters
        mov     ax,.after_call16
        push    0x1234
        push    0xabcd
        call    GDT_SEL_CALL16:0xabcd
.after_call16:
        CHECK16_EQ sp,STACK2_TOP ; parameters are gone from our stack (!)
        mov     ax,ss
        CHECK16_EQ ax,GDT_SEL_UD16|3
        mov     ax,ds
        CHECK16_EQ ax,0 ; cleared because the call gate loaded DS

        ;
        ; Test GP faults with 16-bit handler
        ;

        mov     ax,GDT_SEL_UD16|3
        mov     ds,ax
        lea     bx,[IDT_BASE+EXCEPTION_GP*8]
        mov     word [bx],TestGP_16_16
        mov     word [bx+2],GDT_SEL_ORIG16
        mov     word [bx+4],0xE600
        mov     word [bx+6],0

%macro TEST_GP16_16 2+ ; error code, instruction
        mov     ax,%%cont
        mov     bx,%%inst
        mov     cx,%1
        mov     dx,sp
%%inst:
        %2
        jmp     Fail16
%%cont:
%endmacro
        mov     si,EXCEPTION_GP
        TEST_GP16_16 0,cli
        CHECK16_EQ sp,STACK2_TOP
        push    GDT_SEL_ORIG16
        push    Fail16
        TEST_GP16_16 GDT_SEL_ORIG16,retf
        add     sp,4
        CHECK16_EQ sp,STACK2_TOP

%macro TEST_GP16_16_RETF 3 ; cs,ip,error code
        mov     di,sp
        push    word %1
        push    word %2
        TEST_GP16_16 %3,retf
        add     sp,4
        CHECK16_EQ di,sp
%endmacro

        TEST_GP16_16_RETF 0,0,0 ; CS = 0
        TEST_GP16_16_RETF GDT_SEL_UD16|3,0,GDT_SEL_UD16 ; Not a code segment
        TEST_GP16_16_RETF GDT_SEL_USER16,0,GDT_SEL_USER16 ; RPL < CPL

        cmp     byte [EMU_ID],EMU_ID_DOSBOX ; Dosbox aborts with this command
        je      .skip_gp_1
        TEST_GP16_16 GDT_SEL_NP,call far [cs:.np_dest] ; #GP is raised because DPL is checked before present
.skip_gp_1:

        xor     si,si

        ;
        ; Test #NP
        ;
        lea     bx,[IDT_BASE+EXCEPTION_NP*8]
        mov     word [bx],TestNP_16_16
        mov     word [bx+2],GDT_SEL_ORIG16
        mov     word [bx+4],0xE600
        mov     word [bx+6],0

        mov     si,EXCEPTION_NP
        or      byte [GDT_BASE+GDT_SEL_NP+5],3<<GDT_ACCESS_BIT_DPL
        TEST_GP16_16 GDT_SEL_NP,call far [cs:.np_dest]
        CHECK16_EQ sp,STACK2_TOP

        TEST_GP16_16_RETF GDT_SEL_NP|3,0,GDT_SEL_NP

        xor     si,si

        push    word ~EFLAGS_MASK_TF
        popf
        pushf
        pop     ax
        mov     bx,~(EFLAGS_MASK_ALWAYS0|EFLAGS_MASK_TF|EFLAGS_MASK_IF|3<<EFLAGS_BIT_IOPL)
        mov     cx,bx
        xor     cx,ax
        CHECK16_EQ ax,bx ; IF/IOPL not changed


        ; Done
        mov     bp,TI13_EXIT
        int     0x13
        hlt
.np_dest: dw 0xABCD,GDT_SEL_NP|3

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        bits 32

Test32GPHandler:
        CHECK_EQ [esp],ecx ; error code
        CHECK_EQ [esp+4],eax ; IP
        add     esp,4
        mov     [esp],ebx
        iret

TestGP32:
        ; Can load NULL selector into every register
        mov     ax,0
        mov     ds,ax
        mov     es,ax
        mov     fs,ax
        mov     gs,ax

        mov     ax,GDT_SEL_DATA32
        mov     ds,ax

        SET_HANDLER EXCEPTION_GP,Test32GPHandler

%macro TEST_GP_32 2+
        mov     eax,%%instruction
        mov     ebx,%%next
        mov     ecx,%1
%%instruction:
        %2
%%next:
%endmacro

        TEST_GP_32 0,mov edx,[es:0] ; Access through NULL selector

        mov     dx,GDT_SEL_NULL2
        TEST_GP_32 GDT_SEL_NULL2,mov es,dx

        RESTORE_HANDLER EXCEPTION_GP
        retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%macro SET_TEST_PAGE 2
        mov     dword [PD800+%1*4],%2|PT_MASK_W|PT_MASK_P
%endmacro

%macro PG_FLUSH 0
        push    eax
        mov     eax,cr3
        mov     cr3,eax
        pop     eax
%endmacro

TestPaging:
        ;
        ; Build page tables
        ;

        cld

        mov     ax,GDT_SEL_DATA32
        mov     ds,ax
        mov     es,ax

        mov     ebx,PAGING_BASE

        ; Clear the used pages
        mov     edi,ebx
        mov     ecx,(PAGING_NEXT_PAGE-PAGING_BASE)/4
        xor     eax,eax
        rep     stosd

        ; Each Page Directory Entry maps 4MB (1024 4K pages)
        mov     dword [ebx],PD000|PT_MASK_W|PT_MASK_P
        mov     dword [ebx+(0x80000000/(4096*1024))*4],PD800|PT_MASK_W|PT_MASK_P

        ; Identity map first 1MB
        mov     edi,PD000
        mov     eax,PT_MASK_W|PT_MASK_P
        mov     ecx,(1024*1024)/PAGE_SIZE
.map0:
        stosd
        add     eax,PAGE_SIZE
        dec     ecx
        jnz     .map0

        SET_TEST_PAGE 0,TEST_PAGE0
        SET_TEST_PAGE 1,TEST_PAGE1
        SET_TEST_PAGE 2,TEST_PAGE2

        ; Enable paging
        mov     cr3,ebx
        mov     eax,cr0
        bts     eax,31
        mov     cr0,eax

        call    TestPaging1

        ; Disable paging
        mov     eax,cr0
        btc     eax,31
        mov     cr0,eax

        ret

FillPage: ; EDI = page, AL = value
        cld
        mov     ah,al
        push    ax
        shl     eax,16
        pop     ax
        mov     ecx,PAGE_SIZE/4
        rep     stosd
        ret

TestCrossRead:
        mov     esi,TEST_PAGE0_VIRT+PAGE_SIZE-4
        mov     ecx,5
.loop:
        mov     edx,[esi]
        CHECK_EQ edx,eax
        inc     esi
        shrd    eax,ebx,8
        ror     ebx,8
        loop    .loop
        ret

PageFaultHandler:
        cmp     edx,[esp] ; error code
        je      .ok
        test    edx,edx ; if sign-bit - allow different error code for now...
        CHECK_CC s
.ok:
        CHECK_EQ eax,[esp+4] ; return address
        mov     eax,cr2
        CHECK_EQ eax,ecx ; linear address
        mov     [esp+4],ebx
        add     esp,4
        iretd

TestPaging1:
        ; Initialize test pages
        mov     edi,TEST_PAGE0_VIRT
        mov     al,0xAB
        call    FillPage
        mov     edi,TEST_PAGE1_VIRT
        mov     al,0xCD
        call    FillPage
        mov     edi,TEST_PAGE2_VIRT
        mov     al,0xEF
        call    FillPage

        ; Some basic tests
        CHECK_EQ dword [TEST_PAGE0_VIRT],0xabababab
        CHECK_EQ dword [TEST_PAGE1_VIRT],0xcdcdcdcd
        CHECK_EQ dword [TEST_PAGE2_VIRT],0xefefefef

        ; Crossing a page
        mov     eax,0xabababab
        mov     ebx,0xcdcdcdcd
        call    TestCrossRead

        ; Now swap mapping of page 0 and 1
        SET_TEST_PAGE 0,TEST_PAGE1
        SET_TEST_PAGE 1,TEST_PAGE0
        PG_FLUSH

        ; Virtual cd cd cd cd | ab ab ab ab
        mov     eax,0xcdcdcdcd
        mov     ebx,0xabababab
        call    TestCrossRead

        ; Write and read back
        mov     eax,0x12345678
        ; Virtual cd cd cd 78 | 56 34 12 ab
        mov     dword [TEST_PAGE0_VIRT+PAGE_SIZE-1],eax
        CHECK_EQ dword [TEST_PAGE0_VIRT+PAGE_SIZE-1],eax
        ; And check phsical pages
        CHECK_EQ dword [TEST_PAGE0],0xab123456
        CHECK_EQ dword [TEST_PAGE1+PAGE_SIZE-4],0x78cdcdcd

        ; RMW across a page
        add     dword [TEST_PAGE0_VIRT+PAGE_SIZE-2],0x01020304
        ; Virtual cd cd d1 7b | 58 35 12 ab
        CHECK_EQ dword [TEST_PAGE0_VIRT+PAGE_SIZE-2],0x35587bd1
        mov     eax,[TEST_PAGE0]
        mov     ebx,[TEST_PAGE1+PAGE_SIZE-2]
        CHECK_EQ dword [TEST_PAGE0],0xab123558
        CHECK_EQ dword [TEST_PAGE1+PAGE_SIZE-4],0x7bd1cdcd

        ; Write to physical page and see that it's read back virtually
        mov     dword [TEST_PAGE0],0x11223344
        mov     dword [TEST_PAGE1+PAGE_SIZE-4],0x55667788
        CHECK_EQ dword [TEST_PAGE0_VIRT+PAGE_SIZE-2],0x33445566


        ; movsd across page boundary
        mov     dword [TEST_PAGE0],0x11223344
        mov     dword [TEST_PAGE1+PAGE_SIZE-4],0x55667788
        mov     esi,TEST_PAGE0_VIRT+PAGE_SIZE-2
        mov     edi,TEST_PAGE0_VIRT+PAGE_SIZE-3
        movsd
        mov     eax,0x44556688
        mov     ebx,0x11223333
        call    TestCrossRead

        ; Code crossing boundary
        mov     word [TEST_PAGE0],0xc32a ; (immediate) / ret
        mov     word [TEST_PAGE1+PAGE_SIZE-2],0xb090 ; nop / mov al,imm8
        mov     edx,TEST_PAGE0_VIRT+PAGE_SIZE-2
        mov     eax,0x99999999
        call    edx
        CHECK_EQ eax,0x9999992a

        ;
        ; Check page faults
        ;
        SET_HANDLER EXCEPTION_PF,PageFaultHandler

        GET_LINEAR_BASE ebp ; Keep linear base in EBP

%macro CHECK_PF 3+ ; CR2 / error code
        lea     eax,[ebp+%%inst]
        lea     ebx,[ebp+%%after]
        mov     ecx,%1
        mov     edx,%2
%%inst:
        %3
        call    PModeFail
%%after:
%endmacro

        CHECK_PF TEST_PAGE2_VIRT+PAGE_SIZE,PT_MASK_W,mov dword [TEST_PAGE2_VIRT+PAGE_SIZE-3],0x01020304
        cmp     byte [EMU_ID],EMU_ID_DOSBOX
        je      .dbskip1 ; Oops! Write does happen with DosBox
        mov     eax,dword [TEST_PAGE2_VIRT+PAGE_SIZE-4]
        CHECK_EQ eax,0xefefefef

        mov     edi,TEST_PAGE2_VIRT+PAGE_SIZE-2
        mov     esi,SCRATCH
        mov     dword [esi],0x11223344
        CHECK_PF TEST_PAGE2_VIRT+PAGE_SIZE,PT_MASK_W,movsd
        CHECK_EQ dword [TEST_PAGE2_VIRT+PAGE_SIZE-4],0xefefefef

        ; TODO: Probably shouldn't have W-bit set here, but bochs does..
        CHECK_PF TEST_PAGE2_VIRT+PAGE_SIZE,1<<31,add dword [edi],0x11111111
        CHECK_EQ dword [TEST_PAGE2_VIRT+PAGE_SIZE-4],0xefefefef

        ; PF with pop relative to ESP
        mov     esi,esp
        lea     edi,[esp+1024*1024+4]
        CHECK_PF edi,PT_MASK_W,pop dword [ds:esp+1024*1024]
        CHECK_EQ esi,esp

.dbskip1:
        ; Now a read
        CHECK_PF TEST_PAGE2_VIRT+PAGE_SIZE,0,mov eax,dword [TEST_PAGE2_VIRT+PAGE_SIZE-3]

        RESTORE_HANDLER EXCEPTION_PF

        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        bits 16

        times ROM_SIZE-16-($-$$) hlt

        jmp 0x10000-ROM_SIZE/16:BiosEntry
        times 16-5 db '?'
