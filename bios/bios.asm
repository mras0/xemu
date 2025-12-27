        bits 16
        cpu 8086

%include "common.inc"

BIOS_SIZE EQU 0x1000
BIOS_SEG EQU 0x10000-BIOS_SIZE/16

KBD_STATUS1_RSHIFT EQU 0x01
KBD_STATUS1_LSHIFT EQU 0x02

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InterruptVectors:
        dw      GenericInt              ; 0x00
        dw      GenericInt              ; 0x01
        dw      GenericInt              ; 0x02
        dw      GenericInt              ; 0x03
        dw      GenericInt              ; 0x04
        dw      GenericInt              ; 0x05
        dw      GenericInt              ; 0x06
        dw      GenericInt              ; 0x07
        dw      Int08h_TimerIRQ         ; 0x08 - IRQ0: Timer
        dw      Int09h_KeyboardIRQ      ; 0x09 - IRQ1: Keyboard
        dw      GenericInt              ; 0x0A - IRQ2: Cascade
        dw      GenericInt              ; 0x0B - IRQ3: COM2
        dw      GenericInt              ; 0x0C - IRQ4: COM1
        dw      GenericInt              ; 0x0D - IRQ5: LPT2
        dw      GenericInt              ; 0x0E - IRQ6: Floppy
        dw      GenericInt              ; 0x0F - IRQ7: LPT1 / spurious interrupt
        dw      Int10h_VideoInt         ; 0x10
        dw      Int11h_GetEquipment     ; 0x11
        dw      Int12h_GetMemSize       ; 0x12
        dw      Int13h_DiskInt          ; 0x13
        dw      DummyInt                ; 0x14 - Serial port
        dw      Int15h_Memory           ; 0x15
        dw      Int16h_Keyboard         ; 0x16
        dw      DummyInt                ; 0x17 - Printer
        dw      GenericInt              ; 0x18
        dw      Int19h_WarmReset        ; 0x19
        dw      Int1Ah_Time             ; 0x1A
        dw      GenericInt              ; 0x1B
        dw      GenericInt              ; 0x1C
        dw      GenericInt              ; 0x1D
        dw      GenericInt              ; 0x1E
        dw      GenericInt              ; 0x1F

GenericInt:
        out     HACK_PORT,AL
        hlt
        jmp     GenericInt

DummyInt:
        iret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Util
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

FatalError:
        out     HACK_PORT,AL
        cli
        hlt
        jmp     FatalError
        ret     ; just nearby to allow manual recovery

PutChar: ; Put char in AL
        push    ax
        push    bx
        mov     ah,0x0e
        mov     bx,7
        int     0x10
        cmp     al,10
        jne     .out
        mov     al,13
        int     0x10
.out:
        pop     bx
        pop     ax
        ret

PutStringCS:
        push    ds
        push    cs
        pop     ds
        call    PutString
        pop     ds
        ret

PutLf:
        push    ax
        mov     al,10
        call    PutChar
        pop     ax
        ret

PutString: ; Asciiz string in DS:SI
        push    ax
        push    si
.loop:
        lodsb
        test    al,al
        je      .out
        call    PutChar
        jmp     .loop
.out:
        pop     si
        pop     ax
        ret

PutWordHex: ; Word in AX
        push    ax
        mov     al,ah
        call    PutByteHex
        pop     ax
PutByteHex: ; Byte in AL
        push    ax
        shr     al,1
        shr     al,1
        shr     al,1
        shr     al,1
        call    PutNibbleHex
        pop     ax
PutNibbleHex:
        push    ax
        and     al,0xf
        add     al,'0'
        cmp     al,'9'
        jbe     .print
        add     al,'A'-('0'+10)
.print:
        call    PutChar
        pop     ax
        ret

PutWordDec: ; Word in AX
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        mov     si,.divisors
        xor     bx,bx ; printed
.loop:
        mov     cx,[cs:si]
        inc     si
        inc     si
        xor     dx,dx
        div     cx
        or      bl,al
        je      .next
        add     al,'0'
        call    PutChar
.next:
        mov     ax,dx
        cmp     cx,1
        jne     .loop
        test    bl,bl
        jne     .done
        mov     al,'0'
        call    PutChar
.done:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
.divisors:
        dw      10000
        dw      1000
        dw      100
        dw      10
        dw      1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 10h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%include "int10h.asm"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 11h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

Int11h_GetEquipment:
        push    ds
        mov     ax,BDA_SEG
        mov     ds,ax
        mov     ax,[bda_eqiupment]
        pop     ds
        iret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 12h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Int12h_GetMemSize:
        push    ds
        mov     ax,BDA_SEG
        mov     ds,ax
        mov     ax,[bda_memSizeKB]
        pop     ds
        iret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 13h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Int13h_DiskInt:
        sti
        push    bp
        mov     bp,ax
        mov     al,ah
        mov     ah,0x13
        out     HACK_PORT,ax
        pop     bp
        retf 2 ; return with current flags

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 15h / Memory detection
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Int15h_Memory:
        cmp     ah,0x88         ; GET EXTENDED MEMORY SIZE (286+)
        je      .getextmem
        cmp     ax,0xE820       ; GET SYSTEM MEMORY MAP
        je      .ignore
        cmp     ax,0x2403       ; QUERY A20 GATE SUPPORT
        je      .ignore
        cmp     ah,0xC0         ; SYSTEM - GET CONFIGURATION
        je      .ignore
        cmp     ax,0xE801       ; GET MEMORY SIZE FOR >64M CONFIGURATIONS
        je      .ignore
        cmp     ah,0x53         ; APM
        je      .ignore
        cmp     ah,0x4F         ; Keyboard intercept
        je      .ignore
        cmp     ah,0x91         ; Keyboard ...
        je      .ignore
        cmp     ah,0x41         ; Wait on external event
        je      .notsup
        cmp     ah,0x87         ; Copy extended memory
        je      .notsup
        cmp     ah,0xc2         ; PS/2
        je      .notsup
        jmp     FatalError
.ignore:
        iret
.notsup:
        stc
        mov     ah,0xff
        retf 2
.getextmem:
        push    ds
        mov     ax,BDA_SEG
        mov     ds,ax
        mov     ax,[bda_tempExtMem]
        pop     ds
        clc
        retf 2

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 16h / Keyboard
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%macro KBD_BUF_NEXT 1
        inc     %1
        inc     %1
        cmp     %1,bda_kbdBuffer+32
        jb      %%nowrap
        mov     %1,bda_kbdBuffer
%%nowrap:
%endmacro

Int16h_Keyboard:
        sti
        push    bx
        push    ds
        mov     bx,BDA_SEG
        mov     ds,bx
        cmp     ah,0x92 ; Keyb.cmd capabilities check, just ignore
        je      .out
        cmp     ah,0x11 ; Check for enhanced keystroke
        je      .out
        cmp     ah,0x55 ; MS-DOS
        je      .out
        cmp     ah,0x00
        je      .get
        cmp     ah,0x01
        je      .check
        cmp     ah,0x02
        je      .shift_flags
        jmp     FatalError
.out:
        pop     ds
        pop     bx
        retf 2
.get:
        call    KeyboardPeek
        jz      .get
        cli
        mov     bx,[bda_kbdNextChar]
        KBD_BUF_NEXT bx
        mov     [bda_kbdNextChar],bx
        sti
        jmp     .out
.check:
        call    KeyboardPeek
        jmp     .out
.shift_flags:
        mov     al,[bda_kbdStatus1]
        jmp     .out

KBD_DATA        EQU 0x60
KBD_STAT_CMD    EQU 0x64
KBD_TIMEOUT     EQU 1000

KBD_SCAN_LSHIFT EQU 0x2A
KBD_SCAN_RSHIFT EQU 0x36

KeyboardPeek:
        cli
        mov     bx,[bda_kbdNextChar]
        cmp     bx,[bda_kbdNextFree]    ; Sets ZF
        mov     ax,[bx]
        sti
        ret

KeyboardInit:
        ; Self-test (Note: enables A20 line)
        mov     al,0xAA
        call    KeyboardWriteCommand
        call    KeyboardRead
        cmp     al,0x55
        jne     FatalError

        ; Enable interrupts
        mov     al,0x60
        call    KeyboardWriteCommand
        mov     al,0x61
        out     KBD_DATA,al

        mov     ax,bda_kbdBuffer
        mov     [bda_kbdNextChar],ax
        mov     [bda_kbdNextFree],ax

        xor     ax,ax
        mov     [bda_kbdStatus1],al
        mov     [bda_kbdStatus2],al

        ; Unmask
        cli
        in      al, PIC1_DATA
        and     al, 0xfd                ; Unmask IRQ 1
        out     PIC1_DATA, al
        sti
        ret

KeyboardRead:
        push    cx
        mov     cx,KBD_TIMEOUT
.wait:
        in      al,KBD_STAT_CMD
        test    al,1    ; Check output buffer status (0 = empty)
        loopz   .wait
        jz      FatalError ; Timeout
        in      al,KBD_DATA
        pop     cx
        ret

KeyboardWaitWrite:
        push    ax
        push    cx
        mov     cx,KBD_TIMEOUT
.wait:
        in      al,KBD_STAT_CMD
        test    al,2    ; Check input buffer status (1 = full)
        loopnz  .wait
        jnz     FatalError ; Timeout
        pop     cx
        pop     ax
        ret

KeyboardWriteCommand: ; al = command
        call    KeyboardWaitWrite
        out     KBD_STAT_CMD, al
        ret


Int09h_KeyboardIRQ:
        push    ax
        push    bx
        push    ds
        mov     ax,BDA_SEG
        mov     ds,ax
        xor     ax,ax
        in      al,KBD_DATA

        mov     ah,al           ; Scan code to ah
        and     al,0x7f         ; Mask break code
        cmp     al,KBD_SCAN_LSHIFT
        je      .lshift
        cmp     al,KBD_SCAN_RSHIFT
        je      .rshift
        test    ah,0x80         ; Break?
        jnz     .out
        xor     bh,bh
        mov     bl,al
        add     bx,Kbd1_LowerCase
        test    BYTE [bda_kbdStatus1],KBD_STATUS1_LSHIFT|KBD_STATUS1_RSHIFT
        jz      .gottab
        add     bx,Kbd1_UpperCase-Kbd1_LowerCase
.gottab:
        mov     al,[cs:bx]
        test    al,0x80
        jz      .notnumpad
        xor     al,al ; TODO: Handle numpad
.notnumpad:
        mov     bx,[bda_kbdNextFree]
        mov     [bx],ax
        KBD_BUF_NEXT bx
        cmp     bx,[bda_kbdNextChar]
        je      .out ; Overrun
        mov     [bda_kbdNextFree],bx
.out:
        mov     al,PIC_EOI
        out     PIC1_COMMAND,al
        pop     ds
        pop     bx
        pop     ax
        iret
.lshift:
        mov     al,KBD_STATUS1_LSHIFT
        jmp     .shift
.rshift:
        mov     al,KBD_SCAN_RSHIFT
.shift:
        test    ah,0x80
        jnz     .clear
        or      [bda_kbdStatus1],al
        jmp     .out
.clear:
        not     al
        and     [bda_kbdStatus1],al
        jmp     .out

Kbd1_LowerCase:
        db 0x00,0x1B,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0x2D,0x3D,0x08,0x09 ; 00
        db 0x71,0x77,0x65,0x72,0x74,0x79,0x75,0x69,0x6F,0x70,0x5B,0x5D,0x0D,0x00,0x61,0x73 ; 10
        db 0x64,0x66,0x67,0x68,0x6A,0x6B,0x6C,0x3B,0x27,0x60,0x00,0x5C,0x7A,0x78,0x63,0x76 ; 20
        db 0x62,0x6E,0x6D,0x2C,0x2E,0x2F,0x00,0xAA,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00 ; 30
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB7,0xB8,0xB9,0xAD,0xB4,0xB5,0xB6,0xAB,0xB1 ; 40
        db 0xB2,0xB3,0xB0,0xAE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; 50
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; 60
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; 70

Kbd1_UpperCase:
        db 0x00,0x1B,0x21,0x40,0x23,0x24,0x25,0x5E,0x26,0x2A,0x28,0x29,0x5F,0x2B,0x08,0x09 ; 00
        db 0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0x7B,0x7D,0x0D,0x00,0x41,0x53 ; 10
        db 0x44,0x46,0x47,0x48,0x4A,0x4B,0x4C,0x3A,0x22,0x7E,0x00,0x7C,0x5A,0x58,0x43,0x56 ; 20
        db 0x42,0x4E,0x4D,0x3C,0x3E,0x3F,0x00,0xAA,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00 ; 30
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB7,0xB8,0xB9,0xAD,0xB4,0xB5,0xB6,0xAB,0xB1 ; 40
        db 0xB2,0xB3,0xB0,0xAE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; 50
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; 60

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 19h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Int19h_WarmReset:
        cli
        xor     ax,ax
        mov     ss,ax
        mov     sp,0x8000

        ; Install interrupt vectors
        mov     ax,cs
        mov     ds,ax
        mov     si,InterruptVectors
        xor     di,di
        xor     ax,ax
        mov     es,ax
        mov     cx,0x20
.ivecs:
        movsw
        mov     ax,cs
        stosw
        loop    .ivecs

        call    IntrInit

        sti

        mov     ax, 0x03
        int     0x10

        mov     ax,BDA_SEG
        mov     ds,ax

        mov     si,MsgHello
        call    PutStringCS

        ; Detect base memory (in 16K blocks)
        mov     bp,0x55AA
        xor     bx,bx
.detect:
        mov     es,bx
        mov     [es:0],bp
        cmp     [es:0],bp
        jne     .done
        add     bx,(16*1024)>>4

        xor     ah,ah
        mov     al,bh
        shl     ax,1
        shl     ax,1
        mov     [bda_memSizeKB],ax
        call    PutWordDec
        mov     si,MsgKB
        call    PutStringCS

        cmp     bx,(640*1024)>>4
        jne     .detect
.done:
        cmp     bx,(32*1024)>>4
        jb      FatalError
        mov     al,10
        call    PutChar

        call    DetectExtMem
        mov     [bda_tempExtMem],ax
        call    PutWordDec
        mov     si,MsgExtended
        call    PutStringCS

        ;
        ; Equipment flag
        ;

        ; Detect drives
        xor     dx,dx
.drivedetect:
        xor     ax,ax
        int     0x13
        jc      .gotdrives
        inc     dl
        cmp     dl,4
        jne     .drivedetect
.gotdrives:
        mov     ax,dx
        call    PutWordDec
        mov     si,MsgNumDrives
        call    PutStringCS

        dec     dl
        js      .diskerr ; no drives

        xor     ah,ah
        mov     al,dl
        mov     cl,6
        shl     ax,cl
        or      ax,2<<4|1 ;80x25 / disk available

        mov     WORD [bda_eqiupment], ax

        ;
        ; Fixed disks
        ;
        mov     dx,0x80
.hddetect:
        xor     ax,ax
        int     0x13
        jc      .gothds
        inc     dl
        jmp     .hddetect
.gothds:
        sub     dl,0x80
        mov     [bda_hdCount],dl

        xor     ah,ah
        mov     al,dl
        call    PutWordDec
        mov     si,MsgNumHDs
        call    PutStringCS

        ;
        ; Now initialize other hardware
        ;

        call    TimerInit
        call    KeyboardInit


        ; Hook for host side code
        mov     ax,0x1900
        out     HACK_PORT,ax
        jz      .doboot

        jmp     0x1000:0x0100

.doboot:
        xor     dx,dx
.readboot:
        mov     si,MsgTryingDrive
        call    PutStringCS
        mov     al,dl
        call    PutByteHex
        call    PutLf

        ; Reset disk system
        xor     ah,ah
        int     0x13
        jc      .next

        ; Read boot sector
        mov     ax,0x0201
        mov     cx,1
        xor     bx,bx
        mov     es,bx
        mov     bx,0x7c00
        int     0x13
        jc      .diskerr
        cmp     WORD [es:0x7c00+510],0xaa55
        jne     .next
        jmp     0:0x7c00

.diskerr:
        mov     si,MsgDiskErr
        call    PutStringCS
.next:
        cmp     dl,0x80
        je      .nodisk
        mov     cl,6
        mov     al,[bda_eqiupment]
        shr     al,cl
        cmp     dl,al
        je      .tryhd
        inc     dl
        jmp     .readboot
.tryhd:
        mov     dl,0x80
        jmp     .readboot
.nodisk:
        mov     si,MsgNoDisk
        call    PutStringCS
        xor     ax,ax
        int     0x16
        jmp     .doboot


        cpu 386
DetectExtMem:
        ; A20 enable
        mov     al,2
        out     0x92,al

        ; Load GDT + selector
        lgdt    [cs:.desc]
        mov     eax,cr0
        or      al,1
        mov     cr0,eax
        mov     bx,8
        mov     es,bx
        and     al,0xfe
        mov     cr0,eax

        mov     ebx,1024*1024
        mov     cx,0x55aa
.detect:
        mov     word [es:ebx],cx
        cmp     word [es:ebx],cx
        jne     .done
        add     ebx,64*1024
        jmp     .detect
.done:

        ; A20 disable
        xor     al,al
        out     0x92,al
        mov     eax,ebx
        sub     eax,1024*1024
        shr     eax,10
        ret
.gdt:
        dd      0,0
        dd      0x0000ffff,0x00cf9200
.desc:
        dw      .desc-.gdt-1
        dd      BIOS_SEG*16+.gdt

        cpu     8086

PIC1_COMMAND    EQU 0x20
PIC1_DATA       EQU 0x21
PIC2_COMMAND    EQU 0xA0
PIC2_DATA       EQU 0xA1
PIC_EOI         EQU 0x20
IntrInit:
        mov     al,0x11 ; INIT|ICW4
        out     PIC1_COMMAND,al
        out     PIC2_COMMAND,al
        ; ICW2 (vector offset)
        mov     al,0x08
        out     PIC1_DATA,al
        mov     al,0x70
        out     PIC2_DATA,al
        ; ICW3 (cascade setup)
        mov     al, 1<<2
        out     PIC1_DATA,al
        mov     al, 2
        out     PIC2_DATA,al
        ; ICW4
        mov     al,1
        out     PIC1_DATA,al
        out     PIC2_DATA,al
        ; IMR
        mov     al,0xFF
        out     PIC1_DATA, al
        out     PIC2_DATA, al

        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Int 1Ah / Timer
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Int1Ah_Time:
        cmp     ah,0xb1 ; PCI stuff
        je      .ignore
        sti
        push    bx
        push    ds
        mov     bx,BDA_SEG
        mov     ds,bx
        cmp     ah,(.dispatchEnd-.dispatch)/2
        jae     .oob
        xor     bx,bx
        mov     bl,ah
        add     bl,bl
        call    [cs:.dispatch+bx]
        pop     ds
        pop     bx
        retf 2 ; Return with current flags
.oob:
        jmp     FatalError
.ignore:
        iret
.dispatch:
        dw      .get            ; 00h GET TIME
        dw      .set            ; 01h SET TIME
        dw      .rtc            ; 02h GET RTC TIME
        dw      .rtc            ; 03h SET RTC TIME
        dw      .rtc            ; 04h GET RTC DATE
        dw      .rtc            ; 05h SET RTC DATE
.dispatchEnd:
.get:
        mov     dx,[bda_timerTicks]
        mov     cx,[bda_timerTicks+2]
        mov     al,[bda_timerOverflow]
        ret
.set:
        mov     [bda_timerTicks],dx
        mov     [bda_timerTicks+2],cx
        ret
.rtc:
        stc
        ret

TIMER_CHANNEL0 EQU 0x40
TIMER_COMMAND  EQU 0x43

TimerInit:
        mov     al, 0<<6|3<<4|3<<1      ; Channel 0, access hi/lo byte, mode 3 (square wave), binary
        out     TIMER_COMMAND, al
        xor     ax, ax
        ; Clear ticks
        mov     [bda_timerTicks],ax
        mov     [bda_timerTicks+2],ax
        mov     [bda_timerOverflow],al
        ; Set interval, start timer
        out     TIMER_CHANNEL0, al
        out     TIMER_CHANNEL0, al
        ; Unmask
        cli
        in      al, PIC1_DATA
        and     al, 0xfe                ; Unmask IRQ 0 (PIT)
        out     PIC1_DATA, al
        sti
        ret

Int08h_TimerIRQ:
        push    ax
        push    ds
        mov     ax, BDA_SEG
        mov     ds, ax
        xor     ax, ax
        add     WORD [bda_timerTicks], 1
        adc     WORD [bda_timerTicks], ax
        adc     BYTE [bda_timerOverflow], al
        mov     al, PIC_EOI
        out     PIC1_COMMAND, al
        pop     ds
        pop     ax
        iret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Strings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MsgHello: db "Dummy BIOS starting...",10,0
MsgKB: db " KB",13,0
MsgExtended: db " KB extended memory detected",10,0
MsgNumDrives: db " drive(s) detected",10,0
MsgNumHDs: db " fixed disk(s) detected",10,0
MsgTryingDrive db "Booting from 0x",0
MsgDiskErr: db "Error reading from drive",10,0
MsgNoDisk: db "No bootable disk in drive",10
           db "Insert disk and press any key.",10,0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Reset
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        times BIOS_SIZE-16-($-$$) hlt
        jmp BIOS_SEG:Int19h_WarmReset
        times 16-5 db '?'

%if ($ - $$) != BIOS_SIZE
%assign expectedSize BIOS_SIZE
%assign actualSize ($ - $$)
%error Wrong BIOS size actualSize expected expectedSize
%endif
