#ifndef DECODE_H
#define DECODE_H

#include <cstdint>
#include <functional>
#include <string>
#include "opcodes.h"
#include "address.h"
#include "cpu_registers.h"

enum class CPUModel {
    i8088,
    i8086,
    i80186,
    i80286,
    i80386sx,
    i80386,
    i80486,
    i80586,
};

struct CPUInfo {
    CPUModel model;
    std::uint8_t defaultOperandSize;
};

enum class DecodedEAType {
    none,
    reg8,
    reg16,
    reg32,
    reg64,
    sreg,
    creg,
    dreg,
    mem16,
    mem32,
    abs16_16,
    abs16_32,
    imm8,
    imm16,
    imm32,
    rel8,
    rel16,
    rel32,
    rm16,
    rm32,
};

const char* DecodedEATypeText(DecodedEAType t);

struct DecodedEA {
    DecodedEAType type;
    union {
        std::uint8_t regNum;
        std::uint64_t address;
        std::uint64_t immediate;
#if defined(__GNUC__) && !(defined __clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
#endif
        struct {
            std::uint8_t rm;
            std::uint8_t sib;
            std::uint32_t disp;
        };
#if defined(__GNUC__) && !(defined __clang__)
#pragma GCC diagnostic pop
#endif
    };
};

constexpr uint32_t PREFIX_REPNZ = 1; // F2
constexpr uint32_t PREFIX_REPZ = 2; // F3
constexpr uint32_t PREFIX_REP_MASK = 3;
constexpr uint32_t PREFIX_SEG_SHIFT = 2;
constexpr uint32_t PREFIX_ES = (SREG_ES + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_CS = (SREG_CS + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_SS = (SREG_SS + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_DS = (SREG_DS + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_FS = (SREG_FS + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_GS = (SREG_GS + 1) << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_SEG_MASK = 7 << PREFIX_SEG_SHIFT;
constexpr uint32_t PREFIX_OPER_SIZE = 1 << 5;
constexpr uint32_t PREFIX_ADDR_SIZE = 1 << 6;
constexpr uint32_t PREFIX_LOCK = 1 << 7;

constexpr uint8_t MaxInstructionBytes = 15;

struct InstructionDecodeResult {
    std::uint8_t numInstructionBytes;
    std::uint8_t instructionBytes[MaxInstructionBytes];
    const Instruction* instruction;
    std::uint32_t prefixes;
    std::uint8_t operationSize; // in bytes
    std::uint8_t operandSize;
    std::uint8_t addressSize;
    std::uint32_t opcode;
    InstructionMnem mnemoic;
    std::uint8_t numOperands;
    DecodedEA ea[MaxInstructionOperands];

    uint64_t addressMask() const
    {
        return (uint64_t(1) << 8 * addressSize) - 1;
    }
};

InstructionDecodeResult Decode(const CPUInfo& cpuInfo, std::function<std::uint8_t()> instructionFetch);

using LabelLookupFunc = std::function<std::string (std::uint64_t)>;

std::string FormatDecodedInstruction(const InstructionDecodeResult& ins, const Address& addr, LabelLookupFunc labelLookup = {});
std::string FormatDecodedInstructionFull(const InstructionDecodeResult& ins, const Address& addr, LabelLookupFunc labelLookup = {});

std::string ModrmString(uint8_t modrm);
bool EAIsMemory(DecodedEAType t);

constexpr uint8_t ModrmMod(uint8_t modrm)
{
    return (modrm >> 6) & 3;
}

constexpr uint8_t ModrmReg(uint8_t modrm)
{
    return (modrm >> 3) & 7;
}

constexpr uint8_t ModrmRm(uint8_t modrm)
{
    return modrm & 7;
}

constexpr bool Modrm32HasSib(uint8_t modrm)
{
    if (ModrmMod(modrm) == 0b11)
        return false;
    return ModrmRm(modrm) == 0b100;
}

constexpr bool Modrm32HasDisp(uint8_t modrm)
{
    switch (ModrmMod(modrm)) {
    case 0b00:
        return ModrmRm(modrm) == 0b101;
    case 0b01:
    case 0b10:
        return true;
    default:
        return false;
    }
}

#include <format>

struct DecodedEAInfo {
    DecodedEA ea;
    Address addr; // address of following instruction (for disp)
    std::uint32_t prefixes;
    std::uint8_t memSize;
};

template <>
struct std::formatter<DecodedEA> : std::formatter<const char*> {
    std::format_context::iterator format(const DecodedEA& ea, std::format_context& ctx) const;
};

template <>
struct std::formatter<DecodedEAInfo> : std::formatter<const char*> {
    std::format_context::iterator format(const DecodedEAInfo& info, std::format_context& ctx) const;
};

template <>
struct std::formatter<DecodedEAType> : std::formatter<const char*> {
    std::format_context::iterator format(DecodedEAType type, std::format_context& ctx) const;
};

#endif
