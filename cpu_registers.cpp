#include "cpu_registers.h"

const char* const Reg8Text[8] = { "AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH" };
const char* const Reg16Text[8] = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI" };
const char* const Reg32Text[8] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
const char* const SRegText[8] = { "ES", "CS", "SS", "DS", "FS", "GS", "SREG6", "SREG7" };
const char* const Rm16Text[8] = { "BX+SI", "BX+DI", "BP+SI", "BP+DI", "SI", "DI", "BP", "BX" };