#include "opcodes.h"

static const Instruction ITable_80[8] = {
    { InstructionMnem::ADD, { OperandMode::Eb, OperandMode::Ib } }, // 80/0
    { InstructionMnem::OR, { OperandMode::Eb, OperandMode::Ib } }, // 80/1
    { InstructionMnem::ADC, { OperandMode::Eb, OperandMode::Ib } }, // 80/2
    { InstructionMnem::SBB, { OperandMode::Eb, OperandMode::Ib } }, // 80/3
    { InstructionMnem::AND, { OperandMode::Eb, OperandMode::Ib } }, // 80/4
    { InstructionMnem::SUB, { OperandMode::Eb, OperandMode::Ib } }, // 80/5
    { InstructionMnem::XOR, { OperandMode::Eb, OperandMode::Ib } }, // 80/6
    { InstructionMnem::CMP, { OperandMode::Eb, OperandMode::Ib } }, // 80/7
};

static const Instruction ITable_81[8] = {
    { InstructionMnem::ADD, { OperandMode::Ev, OperandMode::Ivds } }, // 81/0
    { InstructionMnem::OR, { OperandMode::Ev, OperandMode::Ivds } }, // 81/1
    { InstructionMnem::ADC, { OperandMode::Ev, OperandMode::Ivds } }, // 81/2
    { InstructionMnem::SBB, { OperandMode::Ev, OperandMode::Ivds } }, // 81/3
    { InstructionMnem::AND, { OperandMode::Ev, OperandMode::Ivds } }, // 81/4
    { InstructionMnem::SUB, { OperandMode::Ev, OperandMode::Ivds } }, // 81/5
    { InstructionMnem::XOR, { OperandMode::Ev, OperandMode::Ivds } }, // 81/6
    { InstructionMnem::CMP, { OperandMode::Ev, OperandMode::Ivds } }, // 81/7
};

static const Instruction ITable_82[8] = {
    { InstructionMnem::ADD, { OperandMode::Eb, OperandMode::Ib } }, // 82/0
    { InstructionMnem::OR, { OperandMode::Eb, OperandMode::Ib } }, // 82/1
    { InstructionMnem::ADC, { OperandMode::Eb, OperandMode::Ib } }, // 82/2
    { InstructionMnem::SBB, { OperandMode::Eb, OperandMode::Ib } }, // 82/3
    { InstructionMnem::AND, { OperandMode::Eb, OperandMode::Ib } }, // 82/4
    { InstructionMnem::SUB, { OperandMode::Eb, OperandMode::Ib } }, // 82/5
    { InstructionMnem::XOR, { OperandMode::Eb, OperandMode::Ib } }, // 82/6
    { InstructionMnem::CMP, { OperandMode::Eb, OperandMode::Ib } }, // 82/7
};

static const Instruction ITable_83[8] = {
    { InstructionMnem::ADD, { OperandMode::Ev, OperandMode::Ibs } }, // 83/0
    { InstructionMnem::OR, { OperandMode::Ev, OperandMode::Ibs } }, // 83/1
    { InstructionMnem::ADC, { OperandMode::Ev, OperandMode::Ibs } }, // 83/2
    { InstructionMnem::SBB, { OperandMode::Ev, OperandMode::Ibs } }, // 83/3
    { InstructionMnem::AND, { OperandMode::Ev, OperandMode::Ibs } }, // 83/4
    { InstructionMnem::SUB, { OperandMode::Ev, OperandMode::Ibs } }, // 83/5
    { InstructionMnem::XOR, { OperandMode::Ev, OperandMode::Ibs } }, // 83/6
    { InstructionMnem::CMP, { OperandMode::Ev, OperandMode::Ibs } }, // 83/7
};

static const Instruction ITable_8F[8] = {
    { InstructionMnem::POP, { OperandMode::Ev } }, // 8F/0
    { InstructionMnem::UNDEF }, // 8F/1
    { InstructionMnem::UNDEF }, // 8F/2
    { InstructionMnem::UNDEF }, // 8F/3
    { InstructionMnem::UNDEF }, // 8F/4
    { InstructionMnem::UNDEF }, // 8F/5
    { InstructionMnem::UNDEF }, // 8F/6
    { InstructionMnem::UNDEF }, // 8F/7
};

static const Instruction ITable_C0[8] = {
    { InstructionMnem::ROL, { OperandMode::Eb, OperandMode::Ib } }, // C0/0
    { InstructionMnem::ROR, { OperandMode::Eb, OperandMode::Ib } }, // C0/1
    { InstructionMnem::RCL, { OperandMode::Eb, OperandMode::Ib } }, // C0/2
    { InstructionMnem::RCR, { OperandMode::Eb, OperandMode::Ib } }, // C0/3
    { InstructionMnem::SHL, { OperandMode::Eb, OperandMode::Ib } }, // C0/4
    { InstructionMnem::SHR, { OperandMode::Eb, OperandMode::Ib } }, // C0/5
    { InstructionMnem::SAL, { OperandMode::Eb, OperandMode::Ib } }, // C0/6
    { InstructionMnem::SAR, { OperandMode::Eb, OperandMode::Ib } }, // C0/7
};

static const Instruction ITable_C1[8] = {
    { InstructionMnem::ROL, { OperandMode::Ev, OperandMode::Ib } }, // C1/0
    { InstructionMnem::ROR, { OperandMode::Ev, OperandMode::Ib } }, // C1/1
    { InstructionMnem::RCL, { OperandMode::Ev, OperandMode::Ib } }, // C1/2
    { InstructionMnem::RCR, { OperandMode::Ev, OperandMode::Ib } }, // C1/3
    { InstructionMnem::SHL, { OperandMode::Ev, OperandMode::Ib } }, // C1/4
    { InstructionMnem::SHR, { OperandMode::Ev, OperandMode::Ib } }, // C1/5
    { InstructionMnem::SAL, { OperandMode::Ev, OperandMode::Ib } }, // C1/6
    { InstructionMnem::SAR, { OperandMode::Ev, OperandMode::Ib } }, // C1/7
};

static const Instruction ITable_C6[8] = {
    { InstructionMnem::MOV, { OperandMode::Eb, OperandMode::Ib } }, // C6/0
    { InstructionMnem::UNDEF }, // C6/1
    { InstructionMnem::UNDEF }, // C6/2
    { InstructionMnem::UNDEF }, // C6/3
    { InstructionMnem::UNDEF }, // C6/4
    { InstructionMnem::UNDEF }, // C6/5
    { InstructionMnem::UNDEF }, // C6/6
    { InstructionMnem::UNDEF }, // C6/7
};

static const Instruction ITable_C7[8] = {
    { InstructionMnem::MOV, { OperandMode::Ev, OperandMode::Ivds } }, // C7/0
    { InstructionMnem::UNDEF }, // C7/1
    { InstructionMnem::UNDEF }, // C7/2
    { InstructionMnem::UNDEF }, // C7/3
    { InstructionMnem::UNDEF }, // C7/4
    { InstructionMnem::UNDEF }, // C7/5
    { InstructionMnem::UNDEF }, // C7/6
    { InstructionMnem::UNDEF }, // C7/7
};

static const Instruction ITable_D0[8] = {
    { InstructionMnem::ROL, { OperandMode::Eb, OperandMode::C1 } }, // D0/0
    { InstructionMnem::ROR, { OperandMode::Eb, OperandMode::C1 } }, // D0/1
    { InstructionMnem::RCL, { OperandMode::Eb, OperandMode::C1 } }, // D0/2
    { InstructionMnem::RCR, { OperandMode::Eb, OperandMode::C1 } }, // D0/3
    { InstructionMnem::SHL, { OperandMode::Eb, OperandMode::C1 } }, // D0/4
    { InstructionMnem::SHR, { OperandMode::Eb, OperandMode::C1 } }, // D0/5
    { InstructionMnem::SAL, { OperandMode::Eb, OperandMode::C1 } }, // D0/6
    { InstructionMnem::SAR, { OperandMode::Eb, OperandMode::C1 } }, // D0/7
};

static const Instruction ITable_D1[8] = {
    { InstructionMnem::ROL, { OperandMode::Ev, OperandMode::C1 } }, // D1/0
    { InstructionMnem::ROR, { OperandMode::Ev, OperandMode::C1 } }, // D1/1
    { InstructionMnem::RCL, { OperandMode::Ev, OperandMode::C1 } }, // D1/2
    { InstructionMnem::RCR, { OperandMode::Ev, OperandMode::C1 } }, // D1/3
    { InstructionMnem::SHL, { OperandMode::Ev, OperandMode::C1 } }, // D1/4
    { InstructionMnem::SHR, { OperandMode::Ev, OperandMode::C1 } }, // D1/5
    { InstructionMnem::SAL, { OperandMode::Ev, OperandMode::C1 } }, // D1/6
    { InstructionMnem::SAR, { OperandMode::Ev, OperandMode::C1 } }, // D1/7
};

static const Instruction ITable_D2[8] = {
    { InstructionMnem::ROL, { OperandMode::Eb, OperandMode::CL } }, // D2/0
    { InstructionMnem::ROR, { OperandMode::Eb, OperandMode::CL } }, // D2/1
    { InstructionMnem::RCL, { OperandMode::Eb, OperandMode::CL } }, // D2/2
    { InstructionMnem::RCR, { OperandMode::Eb, OperandMode::CL } }, // D2/3
    { InstructionMnem::SHL, { OperandMode::Eb, OperandMode::CL } }, // D2/4
    { InstructionMnem::SHR, { OperandMode::Eb, OperandMode::CL } }, // D2/5
    { InstructionMnem::SAL, { OperandMode::Eb, OperandMode::CL } }, // D2/6
    { InstructionMnem::SAR, { OperandMode::Eb, OperandMode::CL } }, // D2/7
};

static const Instruction ITable_D3[8] = {
    { InstructionMnem::ROL, { OperandMode::Ev, OperandMode::CL } }, // D3/0
    { InstructionMnem::ROR, { OperandMode::Ev, OperandMode::CL } }, // D3/1
    { InstructionMnem::RCL, { OperandMode::Ev, OperandMode::CL } }, // D3/2
    { InstructionMnem::RCR, { OperandMode::Ev, OperandMode::CL } }, // D3/3
    { InstructionMnem::SHL, { OperandMode::Ev, OperandMode::CL } }, // D3/4
    { InstructionMnem::SHR, { OperandMode::Ev, OperandMode::CL } }, // D3/5
    { InstructionMnem::SAL, { OperandMode::Ev, OperandMode::CL } }, // D3/6
    { InstructionMnem::SAR, { OperandMode::Ev, OperandMode::CL } }, // D3/7
};

static const Instruction ITable_D8[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D8/7
};

static const Instruction ITable_D9[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // D9/7
};

static const Instruction ITable_DA[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DA/7
};

static const Instruction ITable_DB[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DB/7
};

static const Instruction ITable_DC[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DC/7
};

static const Instruction ITable_DD[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DD/7
};

static const Instruction ITable_DE[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DE/7
};

static const Instruction ITable_DF[8] = {
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/0
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/1
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/2
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/3
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/4
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/5
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/6
    { InstructionMnem::ESC, { OperandMode::Eb } }, // DF/7
};

static const Instruction ITable_F6[8] = {
    { InstructionMnem::TEST, { OperandMode::Eb, OperandMode::Ib } }, // F6/0
    { InstructionMnem::TEST, { OperandMode::Eb, OperandMode::Ib } }, // F6/1
    { InstructionMnem::NOT, { OperandMode::Eb } }, // F6/2
    { InstructionMnem::NEG, { OperandMode::Eb } }, // F6/3
    { InstructionMnem::MUL, { OperandMode::Eb } }, // F6/4
    { InstructionMnem::IMUL, { OperandMode::Eb } }, // F6/5
    { InstructionMnem::DIV, { OperandMode::Eb } }, // F6/6
    { InstructionMnem::IDIV, { OperandMode::Eb } }, // F6/7
};

static const Instruction ITable_F7[8] = {
    { InstructionMnem::TEST, { OperandMode::Ev, OperandMode::Ivds } }, // F7/0
    { InstructionMnem::TEST, { OperandMode::Ev, OperandMode::Ivds } }, // F7/1
    { InstructionMnem::NOT, { OperandMode::Ev } }, // F7/2
    { InstructionMnem::NEG, { OperandMode::Ev } }, // F7/3
    { InstructionMnem::MUL, { OperandMode::Ev } }, // F7/4
    { InstructionMnem::IMUL, { OperandMode::Ev } }, // F7/5
    { InstructionMnem::DIV, { OperandMode::Ev } }, // F7/6
    { InstructionMnem::IDIV, { OperandMode::Ev } }, // F7/7
};

static const Instruction ITable_FE[8] = {
    { InstructionMnem::INC, { OperandMode::Eb } }, // FE/0
    { InstructionMnem::DEC, { OperandMode::Eb } }, // FE/1
    { InstructionMnem::UNDEF }, // FE/2
    { InstructionMnem::UNDEF }, // FE/3
    { InstructionMnem::UNDEF }, // FE/4
    { InstructionMnem::UNDEF }, // FE/5
    { InstructionMnem::UNDEF }, // FE/6
    { InstructionMnem::UNDEF }, // FE/7
};

static const Instruction ITable_FF[8] = {
    { InstructionMnem::INC, { OperandMode::Ev } }, // FF/0
    { InstructionMnem::DEC, { OperandMode::Ev } }, // FF/1
    { InstructionMnem::CALL, { OperandMode::Ev } }, // FF/2
    { InstructionMnem::CALLF, { OperandMode::Mptp } }, // FF/3
    { InstructionMnem::JMP, { OperandMode::Ev } }, // FF/4
    { InstructionMnem::JMPF, { OperandMode::Mptp } }, // FF/5
    { InstructionMnem::PUSH, { OperandMode::Ev } }, // FF/6
    { InstructionMnem::UNDEF }, // FF/7
};

const Instruction InstructionTable_80386[256] = {
    { InstructionMnem::ADD, { OperandMode::Eb, OperandMode::Gb } }, // 00
    { InstructionMnem::ADD, { OperandMode::Ev, OperandMode::Gv } }, // 01
    { InstructionMnem::ADD, { OperandMode::Gb, OperandMode::Eb } }, // 02
    { InstructionMnem::ADD, { OperandMode::Gv, OperandMode::Ev } }, // 03
    { InstructionMnem::ADD, { OperandMode::AL, OperandMode::Ib } }, // 04
    { InstructionMnem::ADD, { OperandMode::eAX, OperandMode::Ivds } }, // 05
    { InstructionMnem::PUSH, { OperandMode::ES } }, // 06
    { InstructionMnem::POP, { OperandMode::ES } }, // 07
    { InstructionMnem::OR, { OperandMode::Eb, OperandMode::Gb } }, // 08
    { InstructionMnem::OR, { OperandMode::Ev, OperandMode::Gv } }, // 09
    { InstructionMnem::OR, { OperandMode::Gb, OperandMode::Eb } }, // 0A
    { InstructionMnem::OR, { OperandMode::Gv, OperandMode::Ev } }, // 0B
    { InstructionMnem::OR, { OperandMode::AL, OperandMode::Ib } }, // 0C
    { InstructionMnem::OR, { OperandMode::eAX, OperandMode::Ivds } }, // 0D
    { InstructionMnem::PUSH, { OperandMode::CS } }, // 0E
    { InstructionMnem::UNDEF }, // 0F
    { InstructionMnem::ADC, { OperandMode::Eb, OperandMode::Gb } }, // 10
    { InstructionMnem::ADC, { OperandMode::Ev, OperandMode::Gv } }, // 11
    { InstructionMnem::ADC, { OperandMode::Gb, OperandMode::Eb } }, // 12
    { InstructionMnem::ADC, { OperandMode::Gv, OperandMode::Ev } }, // 13
    { InstructionMnem::ADC, { OperandMode::AL, OperandMode::Ib } }, // 14
    { InstructionMnem::ADC, { OperandMode::eAX, OperandMode::Ivds } }, // 15
    { InstructionMnem::PUSH, { OperandMode::SS } }, // 16
    { InstructionMnem::POP, { OperandMode::SS } }, // 17
    { InstructionMnem::SBB, { OperandMode::Eb, OperandMode::Gb } }, // 18
    { InstructionMnem::SBB, { OperandMode::Ev, OperandMode::Gv } }, // 19
    { InstructionMnem::SBB, { OperandMode::Gb, OperandMode::Eb } }, // 1A
    { InstructionMnem::SBB, { OperandMode::Gv, OperandMode::Ev } }, // 1B
    { InstructionMnem::SBB, { OperandMode::AL, OperandMode::Ib } }, // 1C
    { InstructionMnem::SBB, { OperandMode::eAX, OperandMode::Ivds } }, // 1D
    { InstructionMnem::PUSH, { OperandMode::DS } }, // 1E
    { InstructionMnem::POP, { OperandMode::DS } }, // 1F
    { InstructionMnem::AND, { OperandMode::Eb, OperandMode::Gb } }, // 20
    { InstructionMnem::AND, { OperandMode::Ev, OperandMode::Gv } }, // 21
    { InstructionMnem::AND, { OperandMode::Gb, OperandMode::Eb } }, // 22
    { InstructionMnem::AND, { OperandMode::Gv, OperandMode::Ev } }, // 23
    { InstructionMnem::AND, { OperandMode::AL, OperandMode::Ib } }, // 24
    { InstructionMnem::AND, { OperandMode::eAX, OperandMode::Ivds } }, // 25
    { InstructionMnem::PREFIX }, // 26
    { InstructionMnem::DAA }, // 27
    { InstructionMnem::SUB, { OperandMode::Eb, OperandMode::Gb } }, // 28
    { InstructionMnem::SUB, { OperandMode::Ev, OperandMode::Gv } }, // 29
    { InstructionMnem::SUB, { OperandMode::Gb, OperandMode::Eb } }, // 2A
    { InstructionMnem::SUB, { OperandMode::Gv, OperandMode::Ev } }, // 2B
    { InstructionMnem::SUB, { OperandMode::AL, OperandMode::Ib } }, // 2C
    { InstructionMnem::SUB, { OperandMode::eAX, OperandMode::Ivds } }, // 2D
    { InstructionMnem::PREFIX }, // 2E
    { InstructionMnem::DAS }, // 2F
    { InstructionMnem::XOR, { OperandMode::Eb, OperandMode::Gb } }, // 30
    { InstructionMnem::XOR, { OperandMode::Ev, OperandMode::Gv } }, // 31
    { InstructionMnem::XOR, { OperandMode::Gb, OperandMode::Eb } }, // 32
    { InstructionMnem::XOR, { OperandMode::Gv, OperandMode::Ev } }, // 33
    { InstructionMnem::XOR, { OperandMode::AL, OperandMode::Ib } }, // 34
    { InstructionMnem::XOR, { OperandMode::eAX, OperandMode::Ivds } }, // 35
    { InstructionMnem::PREFIX }, // 36
    { InstructionMnem::AAA }, // 37
    { InstructionMnem::CMP, { OperandMode::Eb, OperandMode::Gb } }, // 38
    { InstructionMnem::CMP, { OperandMode::Ev, OperandMode::Gv } }, // 39
    { InstructionMnem::CMP, { OperandMode::Gb, OperandMode::Eb } }, // 3A
    { InstructionMnem::CMP, { OperandMode::Gv, OperandMode::Ev } }, // 3B
    { InstructionMnem::CMP, { OperandMode::AL, OperandMode::Ib } }, // 3C
    { InstructionMnem::CMP, { OperandMode::eAX, OperandMode::Ivds } }, // 3D
    { InstructionMnem::PREFIX }, // 3E
    { InstructionMnem::AAS }, // 3F
    { InstructionMnem::INC, { OperandMode::eAX } }, // 40
    { InstructionMnem::INC, { OperandMode::eCX } }, // 41
    { InstructionMnem::INC, { OperandMode::eDX } }, // 42
    { InstructionMnem::INC, { OperandMode::eBX } }, // 43
    { InstructionMnem::INC, { OperandMode::eSP } }, // 44
    { InstructionMnem::INC, { OperandMode::eBP } }, // 45
    { InstructionMnem::INC, { OperandMode::eSI } }, // 46
    { InstructionMnem::INC, { OperandMode::eDI } }, // 47
    { InstructionMnem::DEC, { OperandMode::eAX } }, // 48
    { InstructionMnem::DEC, { OperandMode::eCX } }, // 49
    { InstructionMnem::DEC, { OperandMode::eDX } }, // 4A
    { InstructionMnem::DEC, { OperandMode::eBX } }, // 4B
    { InstructionMnem::DEC, { OperandMode::eSP } }, // 4C
    { InstructionMnem::DEC, { OperandMode::eBP } }, // 4D
    { InstructionMnem::DEC, { OperandMode::eSI } }, // 4E
    { InstructionMnem::DEC, { OperandMode::eDI } }, // 4F
    { InstructionMnem::PUSH, { OperandMode::eAX } }, // 50
    { InstructionMnem::PUSH, { OperandMode::eCX } }, // 51
    { InstructionMnem::PUSH, { OperandMode::eDX } }, // 52
    { InstructionMnem::PUSH, { OperandMode::eBX } }, // 53
    { InstructionMnem::PUSH, { OperandMode::eSP } }, // 54
    { InstructionMnem::PUSH, { OperandMode::eBP } }, // 55
    { InstructionMnem::PUSH, { OperandMode::eSI } }, // 56
    { InstructionMnem::PUSH, { OperandMode::eDI } }, // 57
    { InstructionMnem::POP, { OperandMode::eAX } }, // 58
    { InstructionMnem::POP, { OperandMode::eCX } }, // 59
    { InstructionMnem::POP, { OperandMode::eDX } }, // 5A
    { InstructionMnem::POP, { OperandMode::eBX } }, // 5B
    { InstructionMnem::POP, { OperandMode::eSP } }, // 5C
    { InstructionMnem::POP, { OperandMode::eBP } }, // 5D
    { InstructionMnem::POP, { OperandMode::eSI } }, // 5E
    { InstructionMnem::POP, { OperandMode::eDI } }, // 5F
    { InstructionMnem::PUSHA }, // 60
    { InstructionMnem::POPA }, // 61
    { InstructionMnem::BOUND, { OperandMode::Gv, OperandMode::Ma } }, // 62
    { InstructionMnem::ARPL, { OperandMode::Ew, OperandMode::Gw } }, // 63
    { InstructionMnem::PREFIX }, // 64
    { InstructionMnem::PREFIX }, // 65
    { InstructionMnem::PREFIX }, // 66
    { InstructionMnem::PREFIX }, // 67
    { InstructionMnem::PUSH, { OperandMode::Ivs } }, // 68
    { InstructionMnem::IMUL, { OperandMode::Gv, OperandMode::Ev, OperandMode::Ivds } }, // 69
    { InstructionMnem::PUSH, { OperandMode::Ibss } }, // 6A
    { InstructionMnem::IMUL, { OperandMode::Gv, OperandMode::Ev, OperandMode::Ibs } }, // 6B
    { InstructionMnem::INSB }, // 6C
    { InstructionMnem::INS }, // 6D
    { InstructionMnem::OUTSB }, // 6E
    { InstructionMnem::OUTS }, // 6F
    { InstructionMnem::JO, { OperandMode::Jbs } }, // 70
    { InstructionMnem::JNO, { OperandMode::Jbs } }, // 71
    { InstructionMnem::JB, { OperandMode::Jbs } }, // 72
    { InstructionMnem::JNB, { OperandMode::Jbs } }, // 73
    { InstructionMnem::JZ, { OperandMode::Jbs } }, // 74
    { InstructionMnem::JNZ, { OperandMode::Jbs } }, // 75
    { InstructionMnem::JBE, { OperandMode::Jbs } }, // 76
    { InstructionMnem::JNBE, { OperandMode::Jbs } }, // 77
    { InstructionMnem::JS, { OperandMode::Jbs } }, // 78
    { InstructionMnem::JNS, { OperandMode::Jbs } }, // 79
    { InstructionMnem::JP, { OperandMode::Jbs } }, // 7A
    { InstructionMnem::JNP, { OperandMode::Jbs } }, // 7B
    { InstructionMnem::JL, { OperandMode::Jbs } }, // 7C
    { InstructionMnem::JNL, { OperandMode::Jbs } }, // 7D
    { InstructionMnem::JLE, { OperandMode::Jbs } }, // 7E
    { InstructionMnem::JNLE, { OperandMode::Jbs } }, // 7F
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_80 }, // 80
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_81 }, // 81
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_82 }, // 82
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_83 }, // 83
    { InstructionMnem::TEST, { OperandMode::Eb, OperandMode::Gb } }, // 84
    { InstructionMnem::TEST, { OperandMode::Ev, OperandMode::Gv } }, // 85
    { InstructionMnem::XCHG, { OperandMode::Gb, OperandMode::Eb } }, // 86
    { InstructionMnem::XCHG, { OperandMode::Gv, OperandMode::Ev } }, // 87
    { InstructionMnem::MOV, { OperandMode::Eb, OperandMode::Gb } }, // 88
    { InstructionMnem::MOV, { OperandMode::Ev, OperandMode::Gv } }, // 89
    { InstructionMnem::MOV, { OperandMode::Gb, OperandMode::Eb } }, // 8A
    { InstructionMnem::MOV, { OperandMode::Gv, OperandMode::Ev } }, // 8B
    { InstructionMnem::MOV, { OperandMode::MwRv, OperandMode::Sw } }, // 8C
    { InstructionMnem::LEA, { OperandMode::Gv, OperandMode::M } }, // 8D
    { InstructionMnem::MOV, { OperandMode::Sw, OperandMode::Ew } }, // 8E
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_8F }, // 8F
    { InstructionMnem::NOP }, // 90
    { InstructionMnem::XCHG, { OperandMode::eCX, OperandMode::eAX } }, // 91
    { InstructionMnem::XCHG, { OperandMode::eDX, OperandMode::eAX } }, // 92
    { InstructionMnem::XCHG, { OperandMode::eBX, OperandMode::eAX } }, // 93
    { InstructionMnem::XCHG, { OperandMode::eSP, OperandMode::eAX } }, // 94
    { InstructionMnem::XCHG, { OperandMode::eBP, OperandMode::eAX } }, // 95
    { InstructionMnem::XCHG, { OperandMode::eSI, OperandMode::eAX } }, // 96
    { InstructionMnem::XCHG, { OperandMode::eDI, OperandMode::eAX } }, // 97
    { InstructionMnem::CWDE }, // 98
    { InstructionMnem::CDQ }, // 99
    { InstructionMnem::CALLF, { OperandMode::Ap } }, // 9A
    { InstructionMnem::FWAIT }, // 9B
    { InstructionMnem::PUSHF }, // 9C
    { InstructionMnem::POPF }, // 9D
    { InstructionMnem::SAHF }, // 9E
    { InstructionMnem::LAHF }, // 9F
    { InstructionMnem::MOV, { OperandMode::AL, OperandMode::Ob } }, // A0
    { InstructionMnem::MOV, { OperandMode::eAX, OperandMode::Ov } }, // A1
    { InstructionMnem::MOV, { OperandMode::Ob, OperandMode::AL } }, // A2
    { InstructionMnem::MOV, { OperandMode::Ov, OperandMode::eAX } }, // A3
    { InstructionMnem::MOVSB }, // A4
    { InstructionMnem::MOVS }, // A5
    { InstructionMnem::CMPSB }, // A6
    { InstructionMnem::CMPS }, // A7
    { InstructionMnem::TEST, { OperandMode::AL, OperandMode::Ib } }, // A8
    { InstructionMnem::TEST, { OperandMode::eAX, OperandMode::Ivds } }, // A9
    { InstructionMnem::STOSB }, // AA
    { InstructionMnem::STOS }, // AB
    { InstructionMnem::LODSB }, // AC
    { InstructionMnem::LODS }, // AD
    { InstructionMnem::SCASB }, // AE
    { InstructionMnem::SCAS }, // AF
    { InstructionMnem::MOV, { OperandMode::AL, OperandMode::Ib } }, // B0
    { InstructionMnem::MOV, { OperandMode::CL, OperandMode::Ib } }, // B1
    { InstructionMnem::MOV, { OperandMode::DL, OperandMode::Ib } }, // B2
    { InstructionMnem::MOV, { OperandMode::BL, OperandMode::Ib } }, // B3
    { InstructionMnem::MOV, { OperandMode::AH, OperandMode::Ib } }, // B4
    { InstructionMnem::MOV, { OperandMode::CH, OperandMode::Ib } }, // B5
    { InstructionMnem::MOV, { OperandMode::DH, OperandMode::Ib } }, // B6
    { InstructionMnem::MOV, { OperandMode::BH, OperandMode::Ib } }, // B7
    { InstructionMnem::MOV, { OperandMode::eAX, OperandMode::Iv } }, // B8
    { InstructionMnem::MOV, { OperandMode::eCX, OperandMode::Iv } }, // B9
    { InstructionMnem::MOV, { OperandMode::eDX, OperandMode::Iv } }, // BA
    { InstructionMnem::MOV, { OperandMode::eBX, OperandMode::Iv } }, // BB
    { InstructionMnem::MOV, { OperandMode::eSP, OperandMode::Iv } }, // BC
    { InstructionMnem::MOV, { OperandMode::eBP, OperandMode::Iv } }, // BD
    { InstructionMnem::MOV, { OperandMode::eSI, OperandMode::Iv } }, // BE
    { InstructionMnem::MOV, { OperandMode::eDI, OperandMode::Iv } }, // BF
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_C0 }, // C0
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_C1 }, // C1
    { InstructionMnem::RETN, { OperandMode::Iw } }, // C2
    { InstructionMnem::RETN }, // C3
    { InstructionMnem::LES, { OperandMode::Gv, OperandMode::Mp } }, // C4
    { InstructionMnem::LDS, { OperandMode::Gv, OperandMode::Mp } }, // C5
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_C6 }, // C6
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_C7 }, // C7
    { InstructionMnem::ENTER, { OperandMode::Iw, OperandMode::Ib } }, // C8
    { InstructionMnem::LEAVE }, // C9
    { InstructionMnem::RETF, { OperandMode::Iw } }, // CA
    { InstructionMnem::RETF }, // CB
    { InstructionMnem::INT3 }, // CC
    { InstructionMnem::INT, { OperandMode::Ib } }, // CD
    { InstructionMnem::INTO }, // CE
    { InstructionMnem::IRET }, // CF
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D0 }, // D0
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D1 }, // D1
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D2 }, // D2
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D3 }, // D3
    { InstructionMnem::AAM, { OperandMode::Ib } }, // D4
    { InstructionMnem::AAD, { OperandMode::Ib } }, // D5
    { InstructionMnem::SALC }, // D6
    { InstructionMnem::XLAT }, // D7
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D8 }, // D8
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_D9 }, // D9
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DA }, // DA
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DB }, // DB
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DC }, // DC
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DD }, // DD
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DE }, // DE
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_DF }, // DF
    { InstructionMnem::LOOPNZ, { OperandMode::Jbs } }, // E0
    { InstructionMnem::LOOPZ, { OperandMode::Jbs } }, // E1
    { InstructionMnem::LOOP, { OperandMode::Jbs } }, // E2
    { InstructionMnem::JCXZ, { OperandMode::Jbs } }, // E3
    { InstructionMnem::IN, { OperandMode::AL, OperandMode::Ib } }, // E4
    { InstructionMnem::IN, { OperandMode::eAX, OperandMode::Ib } }, // E5
    { InstructionMnem::OUT, { OperandMode::Ib, OperandMode::AL } }, // E6
    { InstructionMnem::OUT, { OperandMode::Ib, OperandMode::eAX } }, // E7
    { InstructionMnem::CALL, { OperandMode::Jvds } }, // E8
    { InstructionMnem::JMP, { OperandMode::Jvds } }, // E9
    { InstructionMnem::JMPF, { OperandMode::Ap } }, // EA
    { InstructionMnem::JMP, { OperandMode::Jbs } }, // EB
    { InstructionMnem::IN, { OperandMode::AL, OperandMode::DX } }, // EC
    { InstructionMnem::IN, { OperandMode::eAX, OperandMode::DX } }, // ED
    { InstructionMnem::OUT, { OperandMode::DX, OperandMode::AL } }, // EE
    { InstructionMnem::OUT, { OperandMode::DX, OperandMode::eAX } }, // EF
    { InstructionMnem::PREFIX }, // F0
    { InstructionMnem::INT1 }, // F1
    { InstructionMnem::PREFIX }, // F2
    { InstructionMnem::PREFIX }, // F3
    { InstructionMnem::HLT }, // F4
    { InstructionMnem::CMC }, // F5
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_F6 }, // F6
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_F7 }, // F7
    { InstructionMnem::CLC }, // F8
    { InstructionMnem::STC }, // F9
    { InstructionMnem::CLI }, // FA
    { InstructionMnem::STI }, // FB
    { InstructionMnem::CLD }, // FC
    { InstructionMnem::STD }, // FD
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_FE }, // FE
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_FF }, // FF
};

static const Instruction ITable_0F_00[8] = {
    { InstructionMnem::SLDT, { OperandMode::MwRv } }, // 00/0
    { InstructionMnem::STR, { OperandMode::MwRv } }, // 00/1
    { InstructionMnem::LLDT, { OperandMode::Ew } }, // 00/2
    { InstructionMnem::LTR, { OperandMode::Ew } }, // 00/3
    { InstructionMnem::VERR, { OperandMode::Ew } }, // 00/4
    { InstructionMnem::VERW, { OperandMode::Ew } }, // 00/5
    { InstructionMnem::UNDEF }, // 00/6
    { InstructionMnem::UNDEF }, // 00/7
};

static const Instruction ITable_0F_01[8] = {
    { InstructionMnem::SGDT, { OperandMode::Ms } }, // 01/0
    { InstructionMnem::SIDT, { OperandMode::Ms } }, // 01/1
    { InstructionMnem::LGDT, { OperandMode::Ms } }, // 01/2
    { InstructionMnem::LIDT, { OperandMode::Ms } }, // 01/3
    { InstructionMnem::SMSW, { OperandMode::MwRv } }, // 01/4
    { InstructionMnem::UNDEF }, // 01/5
    { InstructionMnem::LMSW, { OperandMode::Ew } }, // 01/6
    { InstructionMnem::UNDEF }, // 01/7
};

static const Instruction ITable_0F_90[8] = {
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/0
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/1
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/2
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/3
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/4
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/5
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/6
    { InstructionMnem::SETO, { OperandMode::Eb } }, // 90/7
};

static const Instruction ITable_0F_91[8] = {
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/0
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/1
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/2
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/3
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/4
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/5
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/6
    { InstructionMnem::SETNO, { OperandMode::Eb } }, // 91/7
};

static const Instruction ITable_0F_92[8] = {
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/0
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/1
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/2
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/3
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/4
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/5
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/6
    { InstructionMnem::SETB, { OperandMode::Eb } }, // 92/7
};

static const Instruction ITable_0F_93[8] = {
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/0
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/1
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/2
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/3
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/4
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/5
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/6
    { InstructionMnem::SETNB, { OperandMode::Eb } }, // 93/7
};

static const Instruction ITable_0F_94[8] = {
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/0
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/1
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/2
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/3
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/4
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/5
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/6
    { InstructionMnem::SETZ, { OperandMode::Eb } }, // 94/7
};

static const Instruction ITable_0F_95[8] = {
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/0
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/1
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/2
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/3
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/4
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/5
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/6
    { InstructionMnem::SETNZ, { OperandMode::Eb } }, // 95/7
};

static const Instruction ITable_0F_96[8] = {
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/0
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/1
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/2
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/3
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/4
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/5
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/6
    { InstructionMnem::SETBE, { OperandMode::Eb } }, // 96/7
};

static const Instruction ITable_0F_97[8] = {
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/0
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/1
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/2
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/3
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/4
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/5
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/6
    { InstructionMnem::SETNBE, { OperandMode::Eb } }, // 97/7
};

static const Instruction ITable_0F_98[8] = {
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/0
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/1
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/2
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/3
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/4
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/5
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/6
    { InstructionMnem::SETS, { OperandMode::Eb } }, // 98/7
};

static const Instruction ITable_0F_99[8] = {
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/0
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/1
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/2
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/3
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/4
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/5
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/6
    { InstructionMnem::SETNS, { OperandMode::Eb } }, // 99/7
};

static const Instruction ITable_0F_9A[8] = {
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/0
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/1
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/2
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/3
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/4
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/5
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/6
    { InstructionMnem::SETP, { OperandMode::Eb } }, // 9A/7
};

static const Instruction ITable_0F_9B[8] = {
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/0
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/1
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/2
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/3
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/4
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/5
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/6
    { InstructionMnem::SETNP, { OperandMode::Eb } }, // 9B/7
};

static const Instruction ITable_0F_9C[8] = {
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/0
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/1
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/2
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/3
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/4
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/5
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/6
    { InstructionMnem::SETL, { OperandMode::Eb } }, // 9C/7
};

static const Instruction ITable_0F_9D[8] = {
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/0
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/1
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/2
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/3
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/4
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/5
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/6
    { InstructionMnem::SETNL, { OperandMode::Eb } }, // 9D/7
};

static const Instruction ITable_0F_9E[8] = {
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/0
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/1
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/2
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/3
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/4
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/5
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/6
    { InstructionMnem::SETLE, { OperandMode::Eb } }, // 9E/7
};

static const Instruction ITable_0F_9F[8] = {
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/0
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/1
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/2
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/3
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/4
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/5
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/6
    { InstructionMnem::SETNLE, { OperandMode::Eb } }, // 9F/7
};

static const Instruction ITable_0F_BA[8] = {
    { InstructionMnem::UNDEF }, // BA/0
    { InstructionMnem::UNDEF }, // BA/1
    { InstructionMnem::UNDEF }, // BA/2
    { InstructionMnem::UNDEF }, // BA/3
    { InstructionMnem::BT, { OperandMode::Ev, OperandMode::Ib } }, // BA/4
    { InstructionMnem::BTS, { OperandMode::Ev, OperandMode::Ib } }, // BA/5
    { InstructionMnem::BTR, { OperandMode::Ev, OperandMode::Ib } }, // BA/6
    { InstructionMnem::BTC, { OperandMode::Ev, OperandMode::Ib } }, // BA/7
};

const Instruction InstructionTable_0F_80386[256] = {
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_00 }, // 0F 00
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_01 }, // 0F 01
    { InstructionMnem::LAR, { OperandMode::Gv, OperandMode::Mw } }, // 02
    { InstructionMnem::LSL, { OperandMode::Gv, OperandMode::Mw } }, // 03
    { InstructionMnem::UNDEF }, // 04
    { InstructionMnem::LOADALL }, // 05
    { InstructionMnem::CLTS }, // 06
    { InstructionMnem::LOADALL }, // 07
    { InstructionMnem::UNDEF }, // 08
    { InstructionMnem::UNDEF }, // 09
    { InstructionMnem::UNDEF }, // 0A
    { InstructionMnem::UD2 }, // 0B
    { InstructionMnem::UNDEF }, // 0C
    { InstructionMnem::UNDEF }, // 0D
    { InstructionMnem::UNDEF }, // 0E
    { InstructionMnem::UNDEF }, // 0F
    { InstructionMnem::UNDEF }, // 10
    { InstructionMnem::UNDEF }, // 11
    { InstructionMnem::UNDEF }, // 12
    { InstructionMnem::UNDEF }, // 13
    { InstructionMnem::UNDEF }, // 14
    { InstructionMnem::UNDEF }, // 15
    { InstructionMnem::UNDEF }, // 16
    { InstructionMnem::UNDEF }, // 17
    { InstructionMnem::UNDEF }, // 18
    { InstructionMnem::UNDEF }, // 19
    { InstructionMnem::UNDEF }, // 1A
    { InstructionMnem::UNDEF }, // 1B
    { InstructionMnem::UNDEF }, // 1C
    { InstructionMnem::UNDEF }, // 1D
    { InstructionMnem::UNDEF }, // 1E
    { InstructionMnem::UNDEF }, // 1F
    { InstructionMnem::MOV, { OperandMode::Rd, OperandMode::Cd } }, // 20
    { InstructionMnem::MOV, { OperandMode::Rd, OperandMode::Dd } }, // 21
    { InstructionMnem::MOV, { OperandMode::Cd, OperandMode::Rd } }, // 22
    { InstructionMnem::MOV, { OperandMode::Dd, OperandMode::Rd } }, // 23
    { InstructionMnem::MOV, { OperandMode::Rd, OperandMode::Td } }, // 24
    { InstructionMnem::UNDEF }, // 25
    { InstructionMnem::MOV, { OperandMode::Td, OperandMode::Rd } }, // 26
    { InstructionMnem::UNDEF }, // 27
    { InstructionMnem::UNDEF }, // 28
    { InstructionMnem::UNDEF }, // 29
    { InstructionMnem::UNDEF }, // 2A
    { InstructionMnem::UNDEF }, // 2B
    { InstructionMnem::UNDEF }, // 2C
    { InstructionMnem::UNDEF }, // 2D
    { InstructionMnem::UNDEF }, // 2E
    { InstructionMnem::UNDEF }, // 2F
    { InstructionMnem::UNDEF }, // 30
    { InstructionMnem::UNDEF }, // 31
    { InstructionMnem::UNDEF }, // 32
    { InstructionMnem::UNDEF }, // 33
    { InstructionMnem::UNDEF }, // 34
    { InstructionMnem::UNDEF }, // 35
    { InstructionMnem::UNDEF }, // 36
    { InstructionMnem::UNDEF }, // 37
    { InstructionMnem::UNDEF }, // 38
    { InstructionMnem::UNDEF }, // 39
    { InstructionMnem::UNDEF }, // 3A
    { InstructionMnem::UNDEF }, // 3B
    { InstructionMnem::UNDEF }, // 3C
    { InstructionMnem::UNDEF }, // 3D
    { InstructionMnem::UNDEF }, // 3E
    { InstructionMnem::UNDEF }, // 3F
    { InstructionMnem::UNDEF }, // 40
    { InstructionMnem::UNDEF }, // 41
    { InstructionMnem::UNDEF }, // 42
    { InstructionMnem::UNDEF }, // 43
    { InstructionMnem::UNDEF }, // 44
    { InstructionMnem::UNDEF }, // 45
    { InstructionMnem::UNDEF }, // 46
    { InstructionMnem::UNDEF }, // 47
    { InstructionMnem::UNDEF }, // 48
    { InstructionMnem::UNDEF }, // 49
    { InstructionMnem::UNDEF }, // 4A
    { InstructionMnem::UNDEF }, // 4B
    { InstructionMnem::UNDEF }, // 4C
    { InstructionMnem::UNDEF }, // 4D
    { InstructionMnem::UNDEF }, // 4E
    { InstructionMnem::UNDEF }, // 4F
    { InstructionMnem::UNDEF }, // 50
    { InstructionMnem::UNDEF }, // 51
    { InstructionMnem::UNDEF }, // 52
    { InstructionMnem::UNDEF }, // 53
    { InstructionMnem::UNDEF }, // 54
    { InstructionMnem::UNDEF }, // 55
    { InstructionMnem::UNDEF }, // 56
    { InstructionMnem::UNDEF }, // 57
    { InstructionMnem::UNDEF }, // 58
    { InstructionMnem::UNDEF }, // 59
    { InstructionMnem::UNDEF }, // 5A
    { InstructionMnem::UNDEF }, // 5B
    { InstructionMnem::UNDEF }, // 5C
    { InstructionMnem::UNDEF }, // 5D
    { InstructionMnem::UNDEF }, // 5E
    { InstructionMnem::UNDEF }, // 5F
    { InstructionMnem::UNDEF }, // 60
    { InstructionMnem::UNDEF }, // 61
    { InstructionMnem::UNDEF }, // 62
    { InstructionMnem::UNDEF }, // 63
    { InstructionMnem::UNDEF }, // 64
    { InstructionMnem::UNDEF }, // 65
    { InstructionMnem::UNDEF }, // 66
    { InstructionMnem::UNDEF }, // 67
    { InstructionMnem::UNDEF }, // 68
    { InstructionMnem::UNDEF }, // 69
    { InstructionMnem::UNDEF }, // 6A
    { InstructionMnem::UNDEF }, // 6B
    { InstructionMnem::UNDEF }, // 6C
    { InstructionMnem::UNDEF }, // 6D
    { InstructionMnem::UNDEF }, // 6E
    { InstructionMnem::UNDEF }, // 6F
    { InstructionMnem::UNDEF }, // 70
    { InstructionMnem::UNDEF }, // 71
    { InstructionMnem::UNDEF }, // 72
    { InstructionMnem::UNDEF }, // 73
    { InstructionMnem::UNDEF }, // 74
    { InstructionMnem::UNDEF }, // 75
    { InstructionMnem::UNDEF }, // 76
    { InstructionMnem::UNDEF }, // 77
    { InstructionMnem::UNDEF }, // 78
    { InstructionMnem::UNDEF }, // 79
    { InstructionMnem::UNDEF }, // 7A
    { InstructionMnem::UNDEF }, // 7B
    { InstructionMnem::UNDEF }, // 7C
    { InstructionMnem::UNDEF }, // 7D
    { InstructionMnem::UNDEF }, // 7E
    { InstructionMnem::UNDEF }, // 7F
    { InstructionMnem::JO, { OperandMode::Jvds } }, // 80
    { InstructionMnem::JNO, { OperandMode::Jvds } }, // 81
    { InstructionMnem::JB, { OperandMode::Jvds } }, // 82
    { InstructionMnem::JNB, { OperandMode::Jvds } }, // 83
    { InstructionMnem::JZ, { OperandMode::Jvds } }, // 84
    { InstructionMnem::JNZ, { OperandMode::Jvds } }, // 85
    { InstructionMnem::JBE, { OperandMode::Jvds } }, // 86
    { InstructionMnem::JNBE, { OperandMode::Jvds } }, // 87
    { InstructionMnem::JS, { OperandMode::Jvds } }, // 88
    { InstructionMnem::JNS, { OperandMode::Jvds } }, // 89
    { InstructionMnem::JP, { OperandMode::Jvds } }, // 8A
    { InstructionMnem::JNP, { OperandMode::Jvds } }, // 8B
    { InstructionMnem::JL, { OperandMode::Jvds } }, // 8C
    { InstructionMnem::JNL, { OperandMode::Jvds } }, // 8D
    { InstructionMnem::JLE, { OperandMode::Jvds } }, // 8E
    { InstructionMnem::JNLE, { OperandMode::Jvds } }, // 8F
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_90 }, // 0F 90
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_91 }, // 0F 91
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_92 }, // 0F 92
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_93 }, // 0F 93
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_94 }, // 0F 94
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_95 }, // 0F 95
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_96 }, // 0F 96
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_97 }, // 0F 97
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_98 }, // 0F 98
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_99 }, // 0F 99
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9A }, // 0F 9A
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9B }, // 0F 9B
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9C }, // 0F 9C
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9D }, // 0F 9D
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9E }, // 0F 9E
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_9F }, // 0F 9F
    { InstructionMnem::PUSH, { OperandMode::FS } }, // A0
    { InstructionMnem::POP, { OperandMode::FS } }, // A1
    { InstructionMnem::UNDEF }, // A2
    { InstructionMnem::BT, { OperandMode::Ev, OperandMode::Gv } }, // A3
    { InstructionMnem::SHLD, { OperandMode::Ev, OperandMode::Gv, OperandMode::Ib } }, // A4
    { InstructionMnem::SHLD, { OperandMode::Ev, OperandMode::Gv, OperandMode::CL } }, // A5
    { InstructionMnem::UNDEF }, // A6
    { InstructionMnem::UNDEF }, // A7
    { InstructionMnem::PUSH, { OperandMode::GS } }, // A8
    { InstructionMnem::POP, { OperandMode::GS } }, // A9
    { InstructionMnem::RSM }, // AA
    { InstructionMnem::BTS, { OperandMode::Ev, OperandMode::Gv } }, // AB
    { InstructionMnem::SHRD, { OperandMode::Ev, OperandMode::Gv, OperandMode::Ib } }, // AC
    { InstructionMnem::SHRD, { OperandMode::Ev, OperandMode::Gv, OperandMode::CL } }, // AD
    { InstructionMnem::UNDEF }, // AE
    { InstructionMnem::IMUL, { OperandMode::Gv, OperandMode::Ev } }, // AF
    { InstructionMnem::UNDEF }, // B0
    { InstructionMnem::UNDEF }, // B1
    { InstructionMnem::LSS, { OperandMode::Gv, OperandMode::Mptp } }, // B2
    { InstructionMnem::BTR, { OperandMode::Ev, OperandMode::Gv } }, // B3
    { InstructionMnem::LFS, { OperandMode::Gv, OperandMode::Mptp } }, // B4
    { InstructionMnem::LGS, { OperandMode::Gv, OperandMode::Mptp } }, // B5
    { InstructionMnem::MOVZX, { OperandMode::Gv, OperandMode::Eb } }, // B6
    { InstructionMnem::MOVZX, { OperandMode::Gv, OperandMode::Ew } }, // B7
    { InstructionMnem::UNDEF }, // B8
    { InstructionMnem::UD1, { OperandMode::G, OperandMode::E } }, // B9
    { .mnemonic = InstructionMnem::TABLE, .table = ITable_0F_BA }, // 0F BA
    { InstructionMnem::BTC, { OperandMode::Ev, OperandMode::Gv } }, // BB
    { InstructionMnem::BSF, { OperandMode::Gv, OperandMode::Ev } }, // BC
    { InstructionMnem::BSR, { OperandMode::Gv, OperandMode::Ev } }, // BD
    { InstructionMnem::MOVSX, { OperandMode::Gv, OperandMode::Eb } }, // BE
    { InstructionMnem::MOVSX, { OperandMode::Gv, OperandMode::Ew } }, // BF
    { InstructionMnem::UNDEF }, // C0
    { InstructionMnem::UNDEF }, // C1
    { InstructionMnem::UNDEF }, // C2
    { InstructionMnem::UNDEF }, // C3
    { InstructionMnem::UNDEF }, // C4
    { InstructionMnem::UNDEF }, // C5
    { InstructionMnem::UNDEF }, // C6
    { InstructionMnem::UNDEF }, // C7
    { InstructionMnem::UNDEF }, // C8
    { InstructionMnem::UNDEF }, // C9
    { InstructionMnem::UNDEF }, // CA
    { InstructionMnem::UNDEF }, // CB
    { InstructionMnem::UNDEF }, // CC
    { InstructionMnem::UNDEF }, // CD
    { InstructionMnem::UNDEF }, // CE
    { InstructionMnem::UNDEF }, // CF
    { InstructionMnem::UNDEF }, // D0
    { InstructionMnem::UNDEF }, // D1
    { InstructionMnem::UNDEF }, // D2
    { InstructionMnem::UNDEF }, // D3
    { InstructionMnem::UNDEF }, // D4
    { InstructionMnem::UNDEF }, // D5
    { InstructionMnem::UNDEF }, // D6
    { InstructionMnem::UNDEF }, // D7
    { InstructionMnem::UNDEF }, // D8
    { InstructionMnem::UNDEF }, // D9
    { InstructionMnem::UNDEF }, // DA
    { InstructionMnem::UNDEF }, // DB
    { InstructionMnem::UNDEF }, // DC
    { InstructionMnem::UNDEF }, // DD
    { InstructionMnem::UNDEF }, // DE
    { InstructionMnem::UNDEF }, // DF
    { InstructionMnem::UNDEF }, // E0
    { InstructionMnem::UNDEF }, // E1
    { InstructionMnem::UNDEF }, // E2
    { InstructionMnem::UNDEF }, // E3
    { InstructionMnem::UNDEF }, // E4
    { InstructionMnem::UNDEF }, // E5
    { InstructionMnem::UNDEF }, // E6
    { InstructionMnem::UNDEF }, // E7
    { InstructionMnem::UNDEF }, // E8
    { InstructionMnem::UNDEF }, // E9
    { InstructionMnem::UNDEF }, // EA
    { InstructionMnem::UNDEF }, // EB
    { InstructionMnem::UNDEF }, // EC
    { InstructionMnem::UNDEF }, // ED
    { InstructionMnem::UNDEF }, // EE
    { InstructionMnem::UNDEF }, // EF
    { InstructionMnem::UNDEF }, // F0
    { InstructionMnem::UNDEF }, // F1
    { InstructionMnem::UNDEF }, // F2
    { InstructionMnem::UNDEF }, // F3
    { InstructionMnem::UNDEF }, // F4
    { InstructionMnem::UNDEF }, // F5
    { InstructionMnem::UNDEF }, // F6
    { InstructionMnem::UNDEF }, // F7
    { InstructionMnem::UNDEF }, // F8
    { InstructionMnem::UNDEF }, // F9
    { InstructionMnem::UNDEF }, // FA
    { InstructionMnem::UNDEF }, // FB
    { InstructionMnem::UNDEF }, // FC
    { InstructionMnem::UNDEF }, // FD
    { InstructionMnem::UNDEF }, // FE
    { InstructionMnem::UNDEF }, // FF
};

const uint32_t HasModrm1_80386[256/32] = {
    0x0F0F0F0F, 0x0F0F0F0F, 0x00000000, 0x00000A0C, 0x0000FFFF, 0x00000000, 0xFF0F00F3, 0xC0C00000,
};

const uint32_t HasModrm2_80386[256/32] = {
    0x0000000F, 0x0000005F, 0x00000000, 0x00000000, 0xFFFF0000, 0xFEFCB838, 0x00000000, 0x00000000,
};
