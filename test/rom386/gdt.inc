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

GDT_SEL_CODE32 EQU 0x08
GDT_SEL_DATA32 EQU 0x10
GDT_SEL_ORIG16 EQU 0x18
GDT_SEL_DATA16 EQU 0x20
GDT_SEL_TSS    EQU 0x28
GDT_SEL_TSS16  EQU 0x30
GDT_SEL_USER16 EQU 0x38
GDT_SEL_UD16   EQU 0x40
GDT_SEL_CALL16 EQU 0x48
GDT_SEL_NP     EQU 0x50
GDT_SEL_ORIG32 EQU 0x58
GDT_SEL_NULL2  EQU 0x60
GDT_SEL_LDT    EQU 0x68
GDT_SEL_USER32 EQU 0x70
GDT_SEL_UD32   EQU 0x78

%macro GDT_TABLE 0
        dd              0,0
        GDT_ENTRY       0,0x67fff,GDT_ACCESS_CODE,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G ; Funky limit for LSL test
        GDT_ENTRY       0,0xfffff,GDT_ACCESS_DATA,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G
        GDT_ENTRY       0,0xffff,GDT_ACCESS_CODE,0 ; 16-bit code
        GDT_ENTRY       0,0xffff,GDT_ACCESS_DATA,0 ; 16-bit data
        GDT_ENTRY       TSS_BASE,TSS_SIZE,GDT_ACCESS_MASK_P|0x9,0 ; 32-bit TSS
        GDT_ENTRY       TSS16_BASE,TSS16_SIZE,GDT_ACCESS_MASK_P|0x1,0 ; 16-bit TSS
        GDT_ENTRY       0,0xffff,GDT_ACCESS_CODE|3<<GDT_ACCESS_BIT_DPL,0 ; 16-bit code (DPL=3)
        GDT_ENTRY       0,0xffff,GDT_ACCESS_DATA|3<<GDT_ACCESS_BIT_DPL,0 ; 16-bit data (DPL=3)
        ; GDT_SEL_CALL16
        dw              Call16Entry ; offset 15:00
        dw              GDT_SEL_ORIG16 ; selector
        db              2 ; paramter count
        db              GDT_ACCESS_MASK_P|3<<GDT_ACCESS_BIT_DPL|4 ; DPL=3, 16-bit call gate
        dw              0 ; offset 31:16
        ; GDT_SEL_NP
        GDT_ENTRY       0,0xffff,GDT_ACCESS_CODE&~GDT_ACCESS_MASK_P,0
        GDT_ENTRY       0,0xffff,GDT_ACCESS_CODE,GDT_FLAG_MASK_DB ; GDT_SEL_ORIG32
        dd              0,0 ; GDT_SEL_NULL2
        GDT_ENTRY       LDT_BASE,LDT_SIZE-1,GDT_ACCESS_MASK_P|0x2,0 ; LDT
        GDT_ENTRY       0,0xfffff,GDT_ACCESS_CODE|3<<GDT_ACCESS_BIT_DPL,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G ; 32-bit code (DPL=3)
        GDT_ENTRY       0,0xfffff,GDT_ACCESS_DATA|3<<GDT_ACCESS_BIT_DPL,GDT_FLAG_MASK_DB|GDT_FLAG_MASK_G ; 32-bit data (DPL=3)
%endmacro
