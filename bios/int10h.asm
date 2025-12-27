%macro CHECK_PAGE 1
        cmp     %1,7
        ja      FatalError
%endmacro

%macro  IS_TEXT_MODE 0
        cmp     byte [bda_videoMode],3
%endmacro

%macro CHECK_MODE 0
        IS_TEXT_MODE
        jne     FatalError
%endmacro

%macro CURSOR_POS_OFS 0 ; bx = bh*2
        CHECK_PAGE bh
        xor     bl,bl
        xchg    bh,bl
        add     bl,bl
%endmacro

Int10h_VideoInt:
.ok:
        ; VIDEO - MSHERC.COM - GET VIDEO ADAPTER TYPE AND MODE
        cmp     ah,0xEF
        je      .ignore_early
        ; EGA Register Interface Library - INTERROGATE DRIVER
        cmp     ah,0xFA
        je      .ignore_early
        sti
        PUSH_ALL
        cmp     ah,(.dispatchEnd-.dispatch)/2
        jae     .oob
        cld

        ;;
        ;;mov     bp,ax
        ;;mov     ax,0xFEDE
        ;;out     HACK_PORT,ax
        ;;mov     ax,bp
        ;;

        mov     bp,BDA_SEG
        mov     ds,bp
        mov     bp,ax
        xor     al,al
        xchg    al,ah
        add     al,al
        xchg    ax,bp
        call    [cs:.dispatch+bp]

;;;;;;;;;;;;
        ;;mov     ax,BDA_SEG
        ;;mov     ds,ax
        ;;cmp     byte [bda_videoCursorPos],80
        ;;jae     FatalError
        ;;cmp     byte [bda_videoCursorPos+1],CGA_ROWS
        ;;jae     FatalError
;;;;;;;;;;;;

        POP_ALL
.ignore_early:
        iret
.oob:
        jmp     FatalError ; For now
.ignore:
        ret
.dispatch:
        dw      Int10h_00_SetMode               ; 00h
        dw      Int10h_01_SetCursorShape        ; 01h
        dw      Int10h_02_SetCursorPos          ; 02h
        dw      Int10h_03_GetCursorPos          ; 03h
        dw      .oob                            ; 04h
        dw      Int10h_05_SetPage               ; 05h
        dw      Int10h_06_ScrollWindowUp        ; 06h
        dw      Int10h_07_ScrollWindowDown      ; 07h
        dw      Int10h_08_ReadChar              ; 08h
        dw      Int10h_09_WriteCharAttr         ; 09h
        dw      Int10h_0A_WriteCharOnly         ; 0Ah
        dw      .oob                            ; 0Bh
        dw      .oob                            ; 0Ch
        dw      .oob                            ; 0Dh
        dw      Int10h_0E_TTYOut                ; 0Eh
        dw      Int10h_0F_GetMode               ; 0Fh
        dw      .ignore                         ; 10h VGA functions
        dw      .ignore                         ; 11h VGA functions (AX=1104h used by FreeDOS)
        dw      .ignore                         ; 12h Used for EGA detection
        dw      .oob                            ; 13h
        dw      .oob                            ; 14h
        dw      .oob                            ; 15h
        dw      .oob                            ; 16h
        dw      .oob                            ; 17h
        dw      .oob                            ; 18h
        dw      .oob                            ; 19h
        dw      .ignore                         ; 1Ah GET DISPLAY COMBINATION CODE (PS,VGA/MCGA)
        dw      .ignore                         ; 1Bh FUNCTIONALITY/STATE INFORMATION (PS,VGA/MCGA)
        dw      .oob                            ; 1Ch
        dw      .oob                            ; 1Dh
        dw      .oob                            ; 1Eh
        dw      .oob                            ; 1Fh
.dispatchEnd:

CGA_PORT_ADDR EQU 0x3D4
CGA_PORT_DATA EQU 0x3D5
CGA_PORT_MODE EQU 0x3D8
CGA_PORT_CSEL EQU 0x3D9

CGA_ROWS EQU 25
CGA_SEGMENT EQU 0xB800
CGA_MEM_SIZE EQU 16384
VIDEO_CLEAR_WORD EQU 0x0720

CGA_Mode3_80x25_data:
        db 0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00

CGA_Mode6_640x200_data:
        db 0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70, 0x02, 0x01, 0x06, 0x07, 0x00, 0x00

Int10h_00_SetMode:
        cmp     al,3
        je      .mode3
        cmp     al,6
        je      .mode6
        jmp     FatalError
.mode3:
        mov     ah,80
        mov     si,CGA_Mode3_80x25_data
        mov     bx,1<<0|1<<5 ; 80x25 text
        jmp     .gotmode
.mode6:
        mov     ah,80
        mov     si,CGA_Mode6_640x200_data
        mov     bx,0xf<<8|1<<1|1<<2|1<<4 ; 640x200 graphics (B&W), white, intense foreground color
        jmp     .gotmode
.gotmode:

        mov     BYTE [bda_videoMode],al
        mov     BYTE [bda_videoColumns],ah
        mov     BYTE [bda_videoColumns+1],0
        mov     ax,0x1000
        cmp     ah,80
        je      .set_page_size
        mov     ax,0x0800
.set_page_size:
        mov     [bda_videoPageSize],ax ; 4K for 80*25 and 2K for 40*25
        mov     BYTE [bda_videoCurrentPage],0
        mov     BYTE [bda_videoCharRows], CGA_ROWS-1 ; Used by FreeDOS...

        ; Clear cursor position for all pages
        push    ds
        pop     es
        mov     cx,8
        mov     di,bda_videoCursorPos
        xor     ax,ax
        rep     stosw

        ; Clear video mem
        mov     ax,CGA_SEGMENT
        mov     es,ax
        xor     di,di

        xor     ax,ax
        IS_TEXT_MODE
        jnz     .goclear
        mov     ax,VIDEO_CLEAR_WORD
.goclear:
        mov     cx,CGA_MEM_SIZE/2
        rep     stosw

        xor     ah,ah
.setmode:
        cs lodsb
        call    Video_SetReg
        inc     ah
        cmp     ah,0x0E
        jne     .setmode

        mov     dx,CGA_PORT_CSEL
        mov     al,bh
        out     dx,al
        mov     dx,CGA_PORT_MODE
        mov     al,bl
        or      al,1<<3 ; Enable video signal
        out     dx,al

        call    Video_SetCursor

        mov     cx,0x0607
        call    Int10h_01_SetCursorShape

        xor     al,al
        jmp     Video_SetPage

Int10h_01_SetCursorShape: ; CX
        mov     WORD [bda_videoCursorType],cx
        mov     ah,0x0A
        mov     al,ch
        call    Video_SetReg
        inc     ah
        mov     al,cl
        call    Video_SetReg
        ret

Int10h_02_SetCursorPos: ; Row= DH, col = DL, Page (= BH)
        mov     al,bh
        CURSOR_POS_OFS
        mov     [bda_videoCursorPos+bx],dx
        cmp     [bda_videoCurrentPage],al
        je      Video_SetCursor
        ret

Int10h_03_GetCursorPos: ; Page (= BH)
        ;CURSOR_POS_OFS
        mov     cx,[bda_videoCursorType]
        mov     dx,[bda_videoCursorPos+bx]
        mov     bp, sp
        mov     [bp+isf_CX],cx
        mov     [bp+isf_DX],dx
        ret

Int10h_05_SetPage: ; AL=page number
        CHECK_MODE
        CHECK_PAGE al
Video_SetPage:
        mov     [bda_videoCurrentPage],al
        xor     ah,ah
        mul     word [bda_videoPageSize]
        mov     bh,ah
        mov     ah,0x0D
        call    Video_SetReg
        dec     ah
        mov     al,bh
        call    Video_SetReg
        jmp     Video_SetCursor

;
; Window scroling
; AL = number of lines by which to scroll down (00h=clear entire window)
; BH = attribute used to write blank lines at top of window
; CH,CL = row,column of window's upper left corner (y/x)
; DH,DL = row,column of window's lower right corner
;

Int10h_06_ScrollWindowUp:
        call    Video_ScrollWindowSetup
        call    Video_CopyRows
        jmp     Video_ClearWindow

Int10h_07_ScrollWindowDown:
        call    Video_ScrollWindowSetup
        ; Copy in reverse order
        push    di
        xchg    di,si
        mov     ax,bp
        mul     bl
        sub     ax,bp
        add     si,ax
        add     di,ax
        neg     bp
        call    Video_CopyRows
        neg     bp
        pop     di
        jmp     Video_ClearWindow

; Out:
;       BH = Attribute
;       BL = Number of lines to copy
;       CX = 0
;       DL = Number of columns
;       DH = Number of lines to clear
;       SI = Address to bottom of window
;       DI = Address to top of window
;       BP = 2 * bda_videoColumns
;       DS = ES = Video segment
Video_ScrollWindowSetup:
        cmp     byte [bda_videoCurrentPage],0
        jne     FatalError ; TODO
        CHECK_MODE

        mov     bl,al   ; bl
        mov     bp,[bda_videoColumns]
        mov     ax,bp
        mul     ch
        add     al,cl
        adc     ah,0
        add     ax,ax   ; ax = buffer offset
        add     dx,0x0101 ; lower right coordinates were includes
        sub     dl,cl   ; dl = column count
        jbe     .skip
        sub     dh,ch   ; dh = row count
        jbe     .skip
        mov     di,ax   ; di = video buffer offset
        add     bp,bp   ; bp = distance between rows
        xor     ah,ah
        mov     ax,bp
        mul     bl
        mov     si,di
        add     si,ax   ; si = source
        mov     ax,CGA_SEGMENT
        mov     ds,ax
        mov     es,ax
        xor     cx,cx
        cmp     bl,0
        jne     .out
        call    Video_ClearWindow
.skip:
        add     sp,2 ; skip return
        ret
.out:
        mov     al,bl  ; AL = NumLines
        mov     bl,dh  ; BL = WindowHeight
        sub     bl,al  ; BL = WindowHeight-NumLines
        mov     dh,al  ; DH = NumLines
        ret

Video_CopyRows:
        cmp     bl,0
        jne     .loop
        ret
.loop:
        push    si
        push    di
        mov     cl,dl
        rep     movsw
        pop     di
        pop     si
        add     di,bp
        add     si,bp
        dec     bl
        jnz     .loop
        ret

Video_ClearWindow:
        mov     ah,bh
        mov     al,' '  ; ax = fill char/attr
.rowloop:
        push    di
        mov     cl,dl
        rep     stosw
        pop     di
        add     di,bp
        dec     dh
        jnz     .rowloop
        ret


Int10h_08_ReadChar: ; BH = page
        CHECK_MODE
        call    Video_CursorOffset
        mov     ax,CGA_SEGMENT
        mov     es,ax
        mov     ax,[es:bx]
        mov     bp,sp
        mov     [bp+isf_AX],ax
        ret

Int10h_09_WriteCharAttr: ; AL = char, BH = page, BL = Attribute, CX = count
        CHECK_MODE
        mov     ah,bl
        call    Video_CursorOffset
        mov     di,bx
        mov     bx,CGA_SEGMENT
        mov     es,bx
        rep     stosw
        ret

Int10h_0A_WriteCharOnly: ; AL = char, BH = page, BL = Attribute, CX = count
        CHECK_MODE
        call    Video_CursorOffset
        mov     di,bx
        mov     bx,CGA_SEGMENT
        mov     es,bx
.loop:
        mov     [es:di],al
        inc     di
        inc     di
        loop    .loop
        ret

Int10h_0E_TTYOut: ; AL = character, BL = color
        CHECK_MODE
        mov     bh,[bda_videoCurrentPage] ; IBM PC BIOS always writes to current page

        push    bx
        CURSOR_POS_OFS
        mov     si,bx
        add     si,bda_videoCursorPos   ; si = cursor position
        pop     bx

        cmp     al,0x07
        je      .bell
        cmp     al,0x08
        je      .bs
        cmp     al,0x0a
        je      .lf
        cmp     al,0x0d
        je      .cr

        call    Video_CursorOffset
        mov     dx,CGA_SEGMENT
        mov     es,dx
        mov     [es:bx],al

        mov     al,[si]
        inc     al
        cmp     al,[bda_videoColumns]
        jb      .out
        mov     BYTE [si],0
        jmp     .lf

.out:   ; AL = cursor X
        mov     [si],al
.outCursor:
        jmp     Video_SetCursor
.bell:
        ; TODO
        jmp     .out
.bs:
        mov     al,[si]
        dec     al
        js      .bell
        jmp     .out
.lf:
        inc     BYTE [si+1]
        cmp     BYTE [si+1],CGA_ROWS-1
        jbe     .outCursor
        mov     BYTE [si+1],CGA_ROWS-1
        ; Scroll window (TODO: Use AH=06h)
        mov     ax,ax
        mov     al,[bda_videoCurrentPage]
        mul     WORD [bda_videoPageSize]
        mov     di,ax
        mov     ax,[bda_videoColumns]
        mov     bx,CGA_SEGMENT
        mov     es,bx
        push    ds
        mov     ds,bx
        mov     si,ax
        add     si,si                           ; si = 2*columns
        add     si,di
        mov     ah,CGA_ROWS-1
        mul     ah                              ; ax = (rows-1)*columns
        mov     cx,ax
        rep     movsw
        pop     ds
        mov     ax,VIDEO_CLEAR_WORD
        mov     cx,[bda_videoColumns]
        rep     stosw
        jmp     .outCursor
.cr:
        xor     al,al
        jmp     .out


Int10h_0F_GetMode:
        mov     ah, [bda_videoColumns]
        mov     al, [bda_videoMode]
        mov     bh, [bda_videoCurrentPage]
        mov     bp, sp
        mov     [bp+isf_AX],ax
        mov     [bp+isf_BX+1],bh
        ret

Video_CursorOffset: ; BX <- CursorY*bda_videoColumns+CursorX+BH*bda_videoPageSize
        CHECK_PAGE bh
        push    ax
        push    dx
        push    si
        xor     ax,ax
        mov     al,bh
        mul     WORD [bda_videoPageSize]
        mov     si,ax
        CURSOR_POS_OFS
        mov     al,[bda_videoColumns]
        mul     BYTE [bx+bda_videoCursorPos+1]
        add     ax,ax
        add     si,ax
        xor     ax,ax
        mov     al,BYTE [bx+bda_videoCursorPos]
        add     ax,ax
        add     ax,si
        mov     bx,ax
        pop     si
        pop     dx
        pop     ax
        ret

Video_SetCursor:
        mov     bl,[bda_videoCurrentPage]
        add     bl,bl
        xor     bh,bh
        mov     al,[bx+bda_videoCursorPos+1]
        mul     BYTE [bda_videoColumns]
        mov     bl,[bx+bda_videoCursorPos]
        xor     bh,bh
        add     bx,ax

        mov     ah,0x0E
        mov     al,bh
        call    Video_SetReg
        inc     ah
        mov     al,bl
        call    Video_SetReg
        ret

; Set MC6845 register AH=register, AL=value
Video_SetReg:
        push    dx
        mov     dx,CGA_PORT_ADDR
        xchg    ah,al
        out     dx,al
        xchg    ah,al
        inc     dx
        out     dx,al
        pop     dx
        ret

