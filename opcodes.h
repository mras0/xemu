#ifndef OPCODES_H
#define OPCODES_H

#include "opcode_types.h"

extern const Instruction InstructionTable_8086[256];
extern const uint32_t HasModrm1_8086[256 / 32];

extern const Instruction InstructionTable_80386[256];
extern const uint32_t HasModrm1_80386[256 / 32];

extern const Instruction InstructionTable_0F_80386[256];
extern const uint32_t HasModrm2_80386[256 / 32];

inline const char* MnemonicText(InstructionMnem m)
{
    return MnemonicStrings[static_cast<int>(m)];
}

inline const char* OpModeText(OperandMode m)
{
    return ModeStrings[static_cast<int>(m)];
}

#include <format>

template <>
struct std::formatter<InstructionMnem> : std::formatter<const char*> {
    auto format(InstructionMnem m, std::format_context& ctx) const
    {
        return std::formatter<const char*>::format(MnemonicText(m), ctx);
    }
};

template <>
struct std::formatter<OperandMode> : std::formatter<const char*> {
    auto format(OperandMode m, std::format_context& ctx) const
    {
        return std::formatter<const char*>::format(OpModeText(m), ctx);
    }
};

#endif
