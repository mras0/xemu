#ifndef CPU_REGISTERS_H
#define CPU_REGISTERS_H

enum Reg {
    REG_AX,
    REG_CX,
    REG_DX,
    REG_BX,
    REG_SP,
    REG_BP,
    REG_SI,
    REG_DI,
};

enum SReg {
    SREG_ES,
    SREG_CS,
    SREG_SS,
    SREG_DS,
    SREG_FS,
    SREG_GS,
};

extern const char* const Reg8Text[8];
extern const char* const Reg16Text[8];
extern const char* const Reg32Text[8];
extern const char* const SRegText[8];
extern const char* const Rm16Text[8];

#endif