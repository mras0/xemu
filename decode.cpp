#include "decode.h"
#include "opcodes.h"
#include "util.h"
#include <stdexcept>
#include <string>
#include <cassert>
#include <algorithm>

std::string ModrmString(uint8_t modrm)
{
    return "Mod=" + BinString(ModrmMod(modrm), 2) + " Reg=" + BinString(ModrmReg(modrm), 3) + " R/M=" + BinString(ModrmRm(modrm), 3);
}

const char* DecodedEATypeText(DecodedEAType t)
{
    switch (t) {
#define CEAT(x)            \
    case DecodedEAType::x: \
        return #x
        CEAT(none);
        CEAT(reg8);
        CEAT(reg16);
        CEAT(reg32);
        CEAT(reg64);
        CEAT(sreg);
        CEAT(creg);
        CEAT(dreg);
        CEAT(mem16);
        CEAT(mem32);
        CEAT(abs16_16);
        CEAT(abs16_32);
        CEAT(imm8);
        CEAT(imm16);
        CEAT(imm32);
        CEAT(rel8);
        CEAT(rel16);
        CEAT(rel32);
        CEAT(rm16);
        CEAT(rm32);
#undef CEAT
    }
    throw std::runtime_error { "Unknown DecodedEAType " + std::to_string((int)t) };
}

bool EAIsMemory(DecodedEAType t)
{
    switch (t) {
    case DecodedEAType::mem16:
    case DecodedEAType::mem32:
    case DecodedEAType::abs16_16:
    case DecodedEAType::abs16_32:
    case DecodedEAType::rm16:
    case DecodedEAType::rm32:
        return true;
    default:
        return false;
    }
}

std::format_context::iterator std::formatter<DecodedEAType>::format(DecodedEAType type, std::format_context& ctx) const
{
    return std::formatter<const char*>::format(DecodedEATypeText(type), ctx);
}

#if 0
struct DecodedEA {
    DecodedEAType type;
    union {
        std::uint8_t regNum;
        std::uint64_t address;
        std::uint64_t immediate;
        struct {
            std::uint8_t rm;
            std::uint8_t sib;
            std::uint32_t disp;
        };
    };
};
#endif

std::format_context::iterator std::formatter<DecodedEA>::format(const DecodedEA& ea, std::format_context& ctx) const
{
    std::string s = std::format("{} ", ea.type);
    switch (ea.type) {
    case DecodedEAType::reg8:
        s += Reg8Text[ea.regNum];
        break;
    case DecodedEAType::reg16:
        s += Reg16Text[ea.regNum];
        break;
    case DecodedEAType::reg32:
        s += Reg32Text[ea.regNum];
        break;
    case DecodedEAType::rm32:
        s += ModrmString(ea.rm);
        if (Modrm32HasSib(ea.rm))
            s += std::format(" SIB=0x{:02X}", ea.sib);
        if (Modrm32HasDisp(ea.rm))
            s += std::format(" DISP=0x{:X}", ea.disp);
        break;
    default:
        throw std::runtime_error { std::format("TODO: Format DecodedEA {}", ea.type) };
    }
    return std::formatter<const char*>::format(s.c_str(), ctx);
}

struct DecodeTables {
    const Instruction* instructionTable;
    const std::uint32_t* hasModrm;
    const Instruction* instructionTable_0F;
    const std::uint32_t* hasModrm_0F;
};

static constexpr DecodeTables decodeTable_8086 = {
    InstructionTable_8086,
    HasModrm1_8086,
    nullptr,
    nullptr,
};

static constexpr DecodeTables decodeTable_80386 = {
    InstructionTable_80386,
    HasModrm1_80386,
    InstructionTable_0F_80386,
    HasModrm2_80386,
};

static const DecodeTables& GetDecodeTable(const CPUInfo& info)
{
    switch (info.model) {
    case CPUModel::i8088:
    case CPUModel::i8086:
        return decodeTable_8086;
    case CPUModel::i80386sx:
    case CPUModel::i80386:
    case CPUModel::i80586: // For now
        return decodeTable_80386;
    default:
        throw std::runtime_error { "Unsupported CPU model " + std::to_string((int)info.model) };
    }
}

static std::uint8_t ResultSizeFromOpmode(OperandMode opmode, std::uint8_t vSize, InstructionMnem mnemonic)
{
    if (opmode >= OperandMode::AL && opmode <= OperandMode::BH)
        return 1;
    if (opmode >= OperandMode::eAX && opmode <= OperandMode::eDI)
        return vSize;
    if (opmode >= OperandMode::ES && opmode <= OperandMode::GS)
        return 2;

    switch (opmode) {
    case OperandMode::None:
    case OperandMode::C1:
        return 0;
    case OperandMode::DX: // Only for OUT DX
        return 0;

    case OperandMode::Ap:
        return 0;
    case OperandMode::Cd:
    case OperandMode::Dd:
        return 4;
    //case OperandMode::E:
    case OperandMode::Eb:
        return 1;
    case OperandMode::Ev:
        return vSize;
    case OperandMode::Ew:
        return 2;
    //case OperandMode::G:
    case OperandMode::Gb:
        return 1;
    case OperandMode::Gv:
        return vSize;
    case OperandMode::Gw:
        return 2;

    case OperandMode::Ib:
        if (mnemonic == InstructionMnem::AAM)
            return 1;
        [[fallthrough]];
    case OperandMode::Ibs:
    case OperandMode::Ibss:
    case OperandMode::Iv:
    case OperandMode::Ivds:
    case OperandMode::Ivs:
    case OperandMode::Iw:
    case OperandMode::Jbs:
    case OperandMode::Jvds:
        return 0;
    //case OperandMode::M:
    case OperandMode::Ma:
    case OperandMode::Mp:
    case OperandMode::Mptp:
    case OperandMode::Ms:
    case OperandMode::MwRv:
        return 0;
    //case OperandMode::Mw:
    case OperandMode::Ob:
        return 1;
    case OperandMode::Ov:
        return vSize;
    case OperandMode::Rd:
        return 4;
    case OperandMode::Sw:
        return 2;
    //case OperandMode::Td:


    default:
        throw std::runtime_error { std::string("TODO ResultSizeFromOpmode ") + OpModeText(opmode) };
    }
}

static constexpr uint8_t OPCODE_ES = 0x26;
static constexpr uint8_t OPCODE_CS = 0x2E;
static constexpr uint8_t OPCODE_SS = 0x36;
static constexpr uint8_t OPCODE_DS = 0x3E;
static constexpr uint8_t OPCODE_FS = 0x64;
static constexpr uint8_t OPCODE_GS = 0x65;
static constexpr uint8_t OPCODE_OPER = 0x66;
static constexpr uint8_t OPCODE_ADDR = 0x67;
static constexpr uint8_t OPCODE_LOCK = 0xF0;
static constexpr uint8_t OPCODE_REPNZ = 0xF2;
static constexpr uint8_t OPCODE_REPZ = 0xF3;

InstructionDecodeResult Decode(const CPUInfo& cpuInfo, std::function<std::uint8_t()> instructionFetch)
{
    InstructionDecodeResult res {};

    auto ibfetch = [&]() -> std::uint8_t {
        if (res.numInstructionBytes == MaxInstructionBytes) {
            res.mnemoic = InstructionMnem::UNDEF;
            return 0xFF;
        }
        auto ib = instructionFetch();
        res.instructionBytes[res.numInstructionBytes++] = ib;
        return ib;
    };

    auto iwfetch = [&]() {
        std::uint16_t res = ibfetch();
        res |= ibfetch() << 8;
        return res;
    };

    auto idfetch = [&]() {
        std::uint32_t res = iwfetch();
        res |= iwfetch() << 16;
        return res;
    };

    uint8_t opcode;

    const auto& decodeTables = GetDecodeTable(cpuInfo);
    auto instructionTable = decodeTables.instructionTable;
    auto hasModrmTable = decodeTables.hasModrm;

    res.operandSize = cpuInfo.defaultOperandSize;
    res.addressSize = cpuInfo.defaultOperandSize;

    // Prefixes
    for (;;) {
        opcode = ibfetch();
        if (instructionTable[opcode].mnemonic != InstructionMnem::PREFIX)
            break;
        switch (opcode) {
        case OPCODE_ES:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_ES;
            break;
        case OPCODE_CS:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_CS;
            break;
        case OPCODE_SS:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_SS;
            break;
        case OPCODE_DS:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_DS;
            break;
        case OPCODE_FS:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_FS;
            break;
        case OPCODE_GS:
            res.prefixes = (res.prefixes & ~PREFIX_SEG_MASK) | PREFIX_GS;
            break;
        case OPCODE_OPER:
            res.prefixes |= PREFIX_OPER_SIZE;
            res.operandSize = cpuInfo.defaultOperandSize ^ 6;
            break;
        case OPCODE_ADDR:
            res.prefixes |= PREFIX_ADDR_SIZE;
            res.addressSize = cpuInfo.defaultOperandSize ^ 6;
            break;
        case OPCODE_LOCK:
            res.prefixes |= PREFIX_LOCK;
            break;
        case OPCODE_REPNZ:
            res.prefixes = (res.prefixes & ~PREFIX_REP_MASK) | PREFIX_REPNZ;
            break;
        case OPCODE_REPZ:
            res.prefixes = (res.prefixes & ~PREFIX_REP_MASK) | PREFIX_REPZ;
            break;
        default:
            throw std::runtime_error { "TODO: Handle prefix " + HexString(opcode) };
        }
    }

    uint16_t fullOpcode = opcode;

    if (opcode == 0x0F && decodeTables.instructionTable_0F) {
        instructionTable = decodeTables.instructionTable_0F;
        hasModrmTable = decodeTables.hasModrm_0F;
        opcode = ibfetch();
        fullOpcode = fullOpcode << 8 | opcode;
    }

    const auto* ins = &instructionTable[opcode];
    if (ins->mnemonic == InstructionMnem::UNDEF) {
        throw std::runtime_error { "TODO: Undefined instruction " + HexString(fullOpcode) };
    }
    const bool hasModrm = hasModrmTable[opcode / 32] & (1 << (opcode % 32));
    const uint8_t modrm = hasModrm ? ibfetch() : 0;

    if (ins->mnemonic == InstructionMnem::TABLE) {
        assert(hasModrm);
        ins = &ins->table[ModrmReg(modrm)];
    }

    res.mnemoic = ins->mnemonic;
    res.instruction = ins;
    res.opcode = fullOpcode;

    if (ins->operands[0] != OperandMode::None) {
        res.operationSize = ResultSizeFromOpmode(ins->operands[0], res.operandSize, ins->mnemonic);
    } else {
        switch (ins->mnemonic) {
        case InstructionMnem::DAA:
        case InstructionMnem::DAS:
        case InstructionMnem::INSB:
        case InstructionMnem::MOVSB:
        case InstructionMnem::LODSB:
        case InstructionMnem::STOSB:
        case InstructionMnem::SCASB:
        case InstructionMnem::CMPSB:
        case InstructionMnem::OUTSB:
            res.operationSize = 1;
            break;
        default:
            res.operationSize = res.operandSize;
        }
    }

    for (int i = 0; i < MaxInstructionOperands && ins->operands[i] != OperandMode::None; ++i) {
        auto& ea = res.ea[res.numOperands++];
        const auto opmode = ins->operands[i];

        if (opmode >= OperandMode::AL && opmode <= OperandMode::BH) {
            ea.type = DecodedEAType::reg8;
            ea.regNum = static_cast<uint8_t>(static_cast<int>(opmode) - static_cast<int>(OperandMode::AL));
            continue;
        }
        if (opmode >= OperandMode::eAX && opmode <= OperandMode::eDI) {
            ea.type = res.operandSize == 4 ? DecodedEAType::reg32 : DecodedEAType::reg16;
            ea.regNum = static_cast<uint8_t>(static_cast<int>(opmode) - static_cast<int>(OperandMode::eAX));
            continue;
        }
        if (opmode >= OperandMode::ES && opmode <= OperandMode::GS) {
            ea.type = DecodedEAType::sreg;
            ea.regNum = static_cast<uint8_t>(static_cast<int>(opmode) - static_cast<int>(OperandMode::ES));
            continue;
        }
        switch (opmode) {
        case OperandMode::C1:
            ea.type = DecodedEAType::imm8;
            ea.immediate = 1;
            break;
        case OperandMode::DX:
            ea.type = DecodedEAType::reg16;
            ea.regNum = REG_DX;
            break;
        case OperandMode::Ap:
            if (res.operandSize == 4) {
                ea.type = DecodedEAType::abs16_32;
                ea.address = idfetch();
                ea.address |= static_cast<uint64_t>(iwfetch()) << 32;
            } else {
                ea.type = DecodedEAType::abs16_16;
                ea.address = idfetch();
            }
            break;
        case OperandMode::Cd:
            assert(hasModrm);
            ea.type = DecodedEAType::creg;
            ea.regNum = ModrmReg(modrm);
            break;
        case OperandMode::Dd:
            assert(hasModrm);
            ea.type = DecodedEAType::dreg;
            ea.regNum = ModrmReg(modrm);
            break;
        case OperandMode::Eb:
            res.operandSize = 1;
            ea.type = DecodedEAType::reg8;
            goto HandleE;
        case OperandMode::Ew:
            res.operandSize = 2; // Operation sized forced to 2 (e.g. 8C)
            ea.type = DecodedEAType::reg16;
            goto HandleE;
        case OperandMode::Ev:
        handleEv:
            ea.type = res.operandSize == 4 ? DecodedEAType::reg32 : DecodedEAType::reg16;
        HandleE:
            assert(hasModrm);
            if (ModrmMod(modrm) == 0b11) {
                ea.regNum = ModrmRm(modrm);
                break;
            }
            ea.rm = modrm;
            if (res.addressSize == 4) {
                ea.type = DecodedEAType::rm32;
                if (Modrm32HasSib(modrm)) {
                    ea.sib = ibfetch();

                    if ((ea.sib & 7) == REG_BP && ModrmMod(modrm) == 0b00)
                        ea.disp = idfetch();
                }
                if (Modrm32HasDisp(modrm)) {
                    if (ModrmMod(modrm) == 0b01)
                        ea.disp = ibfetch();
                    else
                        ea.disp = idfetch();
                }
            } else {
                ea.type = DecodedEAType::rm16;
                if (ModrmMod(modrm) == 0b01) {
                    ea.disp = ibfetch();
                } else if (ModrmMod(modrm) == 0b10 || (ModrmMod(modrm) == 0b00 && ModrmRm(modrm) == 0b110)) {
                    ea.disp = iwfetch();
                }
            }
            break;
        case OperandMode::Gb:
            assert(hasModrm);
            ea.type = DecodedEAType::reg8;
            ea.regNum = ModrmReg(modrm);
            break;
        case OperandMode::Gv:
            assert(hasModrm);
            ea.type = res.operandSize == 4 ? DecodedEAType::reg32 : DecodedEAType::reg16;
            ea.regNum = ModrmReg(modrm);
            break;
        case OperandMode::Gw:
            assert(hasModrm);
            ea.type = DecodedEAType::reg16;
            ea.regNum = ModrmReg(modrm);
            break;
        case OperandMode::Ib:
        case OperandMode::Ibs:
            ea.type = DecodedEAType::imm8;
            ea.immediate = ibfetch();
            break;
        case OperandMode::Ibss:
            ea.type = DecodedEAType::imm8;
            ea.immediate = static_cast<int64_t>(static_cast<int8_t>(ibfetch()));
            break;
        case OperandMode::Ivs:
            if (res.operandSize == 4) {
                ea.type = DecodedEAType::imm32;
                ea.immediate = static_cast<int64_t>(static_cast<int32_t>(idfetch()));
            } else {
                ea.type = DecodedEAType::imm16;
                ea.immediate = static_cast<int64_t>(static_cast<int16_t>(iwfetch()));
            }
            break;
        case OperandMode::Iv:
        case OperandMode::Ivds:
            if (res.operandSize == 4) {
                ea.type = DecodedEAType::imm32;
                ea.immediate = idfetch();
            } else {
                ea.type = DecodedEAType::imm16;
                ea.immediate = iwfetch();
            }
            break;
        case OperandMode::Iw:
            ea.type = DecodedEAType::imm16;
            ea.immediate = iwfetch();
            break;
        case OperandMode::Jbs:
            ea.type = DecodedEAType::rel8;
            ea.immediate = ibfetch();
            break;
        case OperandMode::Jvds:
            if (res.operandSize == 4) {
                ea.type = DecodedEAType::rel32;
                ea.immediate = idfetch();
            } else {
                ea.type = DecodedEAType::rel16;
                ea.immediate = iwfetch();
            }
            break;
        case OperandMode::Ob:
            res.operandSize = 1;
            goto HandleO;
        case OperandMode::Ov:
        HandleO:
            if (res.addressSize == 2) {
                ea.type = DecodedEAType::mem16;
                ea.address = iwfetch();
            } else {
                ea.type = DecodedEAType::mem32;
                ea.address = idfetch();
            }
            break;
        case OperandMode::M:
        case OperandMode::Ma: // TODO: Can be 16/16 or 32/32
        case OperandMode::Mp:
        case OperandMode::Mptp: // TODO: Can be 16:64
        case OperandMode::Ms: // TODO: Can be 16:64
            assert(hasModrm);
            if (ModrmMod(modrm) == 0b11)
                goto handleEv; // This will should cause an #UD later on, but allow decoding
            goto HandleE;
        case OperandMode::Rd:
            assert(hasModrm);
            ea.type = DecodedEAType::reg32;
            if (ModrmMod(modrm) != 0b11)
                throw std::runtime_error { std::string("Invalid for 'R': ") + OpModeText(opmode) + " INS " + MnemonicText(ins->mnemonic) + " OPCODE " + HexString(fullOpcode) + " " + ModrmString(modrm) };
            ea.regNum = ModrmRm(modrm);
            break;
        case OperandMode::Sw:
            assert(hasModrm);
            ea.type = DecodedEAType::sreg;
            ea.regNum = ModrmReg(modrm);
            if (cpuInfo.model < CPUModel::i80386sx)
                ea.regNum &= 3; // Only lower two bits used
            break;
        case OperandMode::MwRv:
            // 8C MOV r/m, Sreg is a bit tricky. It's "Ew" when the destination is memory, but "Ev" when it's a register
            if (ModrmMod(modrm) == 0b11) {
                res.operationSize = res.operandSize;
                goto handleEv;
            }
            res.operandSize = 2;
            res.operationSize = 2;
            goto HandleE;
        default:
            throw std::runtime_error { std::string("TODO [EA] opmode ") + OpModeText(opmode) + " INS " + MnemonicText(ins->mnemonic) + " OPCODE " + HexString(fullOpcode) };
        }
    }

    return res;
}

static const char* SegOverrideString(std::uint32_t prefixes)
{
    switch (prefixes & PREFIX_SEG_MASK) {
    case 0:
        return "";
    case PREFIX_ES:
        return "ES:";
    case PREFIX_CS:
        return "CS:";
    case PREFIX_SS:
        return "SS:";
    case PREFIX_DS:
        return "DS:";
    case PREFIX_FS:
        return "FS:";
    case PREFIX_GS:
        return "GS:";
    default:
        throw std::runtime_error {"TODO: SegOverrideString for prefixes=0x"+HexString(prefixes)};
    }
}

static const char* SegStringFromPrefix(std::uint8_t prefix)
{
    switch (prefix) {
    case OPCODE_ES:
        return "ES";
    case OPCODE_CS:
        return "CS";
    case OPCODE_SS:
        return "SS";
    case OPCODE_DS:
        return "DS";
    case OPCODE_FS:
        return "FS";
    case OPCODE_GS:
        return "GS";
    default:
        throw std::runtime_error { "TODO: SegStringFromPrefix for prefixes=0x" + HexString(prefix) };
    }
}

std::format_context::iterator std::formatter<DecodedEAInfo>::format(const DecodedEAInfo& info, std::format_context& ctx) const
{
    char buffer[32];
    const auto& ea = info.ea;

    auto disp_string = [](int disp, size_t width) {
        std::string s;
        if (disp < 0) {
            s += '-';
            disp = -disp;
        } else {
            s += "+";
        }
        s += "0x";
        s += HexString(disp, width);
        return s;
    };

    auto RelAddress = [&](const Address& a, size_t width = 4) {
        // TODO: Depending on processor mode...
        *std::format_to_n(buffer, sizeof(buffer), "0x{:0{}X}", a.offset(), width).out = '\0';
    };

    std::string mem;
    switch (info.memSize) {
    case 0:
        break;
    case 1:
        mem = "BYTE ";
        break;
    case 2:
        mem = "WORD ";
        break;
    case 4:
        mem = "DWORD ";
        break;
    default:
        throw std::runtime_error { std::format("Invalid memSize={}", info.memSize) };
    }

    mem += "[";
    mem += SegOverrideString(info.prefixes);

    switch (ea.type) {
    case DecodedEAType::reg8:
        assert(ea.regNum < 8);
        return std::formatter<const char*>::format(Reg8Text[ea.regNum], ctx);
    case DecodedEAType::reg16:
        assert(ea.regNum < 8);
        return std::formatter<const char*>::format(Reg16Text[ea.regNum], ctx);
    case DecodedEAType::reg32:
        assert(ea.regNum < 8);
        return std::formatter<const char*>::format(Reg32Text[ea.regNum], ctx);
    case DecodedEAType::sreg:
        assert(ea.regNum < 8);
        return std::formatter<const char*>::format(SRegText[ea.regNum], ctx);
    case DecodedEAType::creg:
        *std::format_to_n(buffer, sizeof(buffer), "CR{}", ea.regNum).out = '\0';
        break;
    case DecodedEAType::dreg:
        *std::format_to_n(buffer, sizeof(buffer), "DR{}", ea.regNum).out = '\0';
        break;
    case DecodedEAType::mem16:
        *std::format_to_n(buffer, sizeof(buffer), "{}0x{:04X}]", mem, ea.address & 0xffff).out = '\0';
        break;
    case DecodedEAType::mem32:
        *std::format_to_n(buffer, sizeof(buffer), "{}0x{:08X}]", mem, ea.address & 0xffffffff).out = '\0';
        break;
    case DecodedEAType::abs16_16:
        *std::format_to_n(buffer, sizeof(buffer), "0x{:04X}:0x{:04X}", ea.address >> 16, ea.address & 0xffff).out = '\0';
        break;
    case DecodedEAType::abs16_32:
        *std::format_to_n(buffer, sizeof(buffer), "0x{:04X}:0x{:08X}", ea.address >> 32, ea.address & 0xffffffff).out = '\0';
        break;
    case DecodedEAType::imm8:
        *std::format_to_n(buffer, sizeof(buffer), "0x{:02X}", ea.immediate & 0xff).out = '\0';
        break;
    case DecodedEAType::imm16:
        *std::format_to_n(buffer, sizeof(buffer), "0x{:04X}", ea.immediate & 0xffff).out = '\0';
        break;
    case DecodedEAType::imm32:
        *std::format_to_n(buffer, sizeof(buffer), "0x{:08X}", ea.immediate & 0xffffffff).out = '\0';
        break;
    case DecodedEAType::rel8:
        RelAddress(info.addr + static_cast<int8_t>(ea.immediate & 0xff));
        break;
    case DecodedEAType::rel16:
        RelAddress(info.addr + static_cast<int16_t>(ea.immediate & 0xffff));
        break;
    case DecodedEAType::rel32:
        RelAddress(info.addr + static_cast<int32_t>(ea.immediate & 0xffffffff), 8);
        break;
    case DecodedEAType::rm16: {
        switch (ModrmMod(ea.rm)) {
        case 0b00:
            if (ModrmRm(ea.rm) == 0b110) {
                *std::format_to_n(buffer, sizeof(buffer), "{}0x{:04X}]", mem, ea.disp).out = '\0';
                break;
            }
            *std::format_to_n(buffer, sizeof(buffer), "{}{}]", mem, Rm16Text[ModrmRm(ea.rm)]).out = '\0';
            break;
        case 0b01:
            *std::format_to_n(buffer, sizeof(buffer), "{}{}{}]", mem, Rm16Text[ModrmRm(ea.rm)], disp_string(static_cast<int8_t>(ea.disp), 2)).out = '\0';
            break;
        case 0b10:
            *std::format_to_n(buffer, sizeof(buffer), "{}{}{}]", mem, Rm16Text[ModrmRm(ea.rm)], disp_string(static_cast<int16_t>(ea.disp), 4)).out = '\0';
            break;
        default:
            throw std::runtime_error { "TODO: Format rm16 " + ModrmString(ea.rm) };
        }
        break;
    }
    case DecodedEAType::rm32: {
        std::string disp;
        const auto mod = ModrmMod(ea.rm);
        const auto rm = ModrmRm(ea.rm);
        if (mod == 0b01)
            disp = disp_string(static_cast<int8_t>(ea.disp), 2);
        else if (mod == 0b10 || (mod == 0b00 && rm == 0b101))
            disp = disp_string(static_cast<int32_t>(ea.disp), 8);

        if (Modrm32HasSib(ea.rm)) {
            const auto scale = 1 << ((ea.sib >> 6) & 3);
            const auto index = (ea.sib >> 3) & 7;
            const auto base = ea.sib & 7;

            if (base == REG_BP && mod == 0b00) {
                if (index == REG_SP) {
                    *std::format_to_n(buffer, sizeof(buffer), "{}0x{:08X}]", mem, ea.disp).out = '\0';
                    break;
                }
                *std::format_to_n(buffer, sizeof(buffer), "{}{}*{}{}]", mem, Reg32Text[index], scale, disp_string(static_cast<int32_t>(ea.disp), 8)).out = '\0';
                break;
            }

            if (index == REG_SP) {
                *std::format_to_n(buffer, sizeof(buffer), "{}{}{}]", mem, Reg32Text[base], disp).out = '\0';
                break;
            }
            *std::format_to_n(buffer, sizeof(buffer), "{}{}+{}*{}{}]", mem, Reg32Text[base], Reg32Text[index], scale, disp).out = '\0';
            break;
        }

        switch (mod) {
        case 0b00:
            if (rm == 0b101) {
                *std::format_to_n(buffer, sizeof(buffer), "{}0x{:08X}]", mem, ea.disp).out = '\0';
                break;
            }
            assert(rm != 0b100);
            *std::format_to_n(buffer, sizeof(buffer), "{}{}]", mem, Reg32Text[rm]).out = '\0';
            break;
        case 0b01:
            *std::format_to_n(buffer, sizeof(buffer), "{}{}{}]", mem, Reg32Text[ModrmRm(ea.rm)], disp_string(static_cast<int8_t>(ea.disp), 2)).out = '\0';
            break;
        case 0b10:
            *std::format_to_n(buffer, sizeof(buffer), "{}{}{}]", mem, Reg32Text[ModrmRm(ea.rm)], disp_string(static_cast<int8_t>(ea.disp), 8)).out = '\0';
            break;
        default:
            throw std::runtime_error { "TODO: Format rm32 " + ModrmString(ea.rm) };
        }
        break;
    }
    default:
        throw std::runtime_error { std::string("format: Unknown DecodedEAType ") + DecodedEATypeText(ea.type) };
    }
    return std::formatter<const char*>::format(buffer, ctx);
}


std::string FormatDecodedInstruction(const InstructionDecodeResult& ins, const Address& addr)
{
    std::string res = "";

    //
    // First one pass over the prefixes to determine which one is the active one
    //
    int posSeg = -1;
    int posRep = -1;
    int posPrefixEnd = 0;

    for (int i = 0; i < ins.numInstructionBytes; ++i) {
        const auto opcode = ins.instructionBytes[i];
        if (opcode == (ins.opcode & 0xff) || opcode == (ins.opcode >> 8)) {
            posPrefixEnd = i;
            break;
        }
        switch (opcode) {
        case OPCODE_ES:
        case OPCODE_CS:
        case OPCODE_SS:
        case OPCODE_DS:
        case OPCODE_FS:
        case OPCODE_GS:
            posSeg = i;
            break;
        case OPCODE_OPER:
        case OPCODE_ADDR:
        case OPCODE_LOCK:
            break;
        case OPCODE_REPNZ:
        case OPCODE_REPZ:
            posRep = i;
            break;
        default:
            assert(false); // Unknown prefix? (Or wrong classification)
        }
    }

    //
    // Second pass to print unused ones (needs to be improved for e.g. oper/addr/lock
    //
    const bool hasMemOperand = std::any_of(ins.ea, ins.ea + ins.numOperands, [](const auto& ea) { return EAIsMemory(ea.type); });
    for (int i = 0; i < posPrefixEnd; ++i) {
        const auto opcode = ins.instructionBytes[i];
        switch (opcode) {
        case OPCODE_ES:
        case OPCODE_CS:
        case OPCODE_SS:
        case OPCODE_DS:
        case OPCODE_FS:
        case OPCODE_GS:
            if (posSeg != i || !hasMemOperand) {
                res += SegStringFromPrefix(opcode);
                res += " ";
            }
            break;
        case OPCODE_OPER:
        case OPCODE_ADDR:
            break; // TODO: These should be printed if not consumed by the instruction (but probably hard to determine...)
        case OPCODE_LOCK:
            res += "LOCK ";
            break;
        case OPCODE_REPNZ:
            res += "REPNZ ";
            break;
        case OPCODE_REPZ:
            if (posRep != i) {
                res += "REPZ ";
                break;
            }
            switch (ins.instruction->mnemonic) {
            case InstructionMnem::INS:
            case InstructionMnem::INSB:
            case InstructionMnem::MOVS:
            case InstructionMnem::MOVSB:
            case InstructionMnem::LODS:
            case InstructionMnem::LODSB:
            case InstructionMnem::STOS:
            case InstructionMnem::STOSB:
            case InstructionMnem::OUTS:
            case InstructionMnem::OUTSB:
                res += "REP ";
                break;
            default:
                res += "REPZ ";
            }
            break;
        default:
            assert(false); // Unknown prefix? (Or wrong classification)
        }
    }

    res += std::format("{}", ins.mnemoic);

    std::uint8_t memSize = 0;
    switch (ins.instruction->mnemonic) {
    case InstructionMnem::INS:
    case InstructionMnem::MOVS:
    case InstructionMnem::LODS:
    case InstructionMnem::STOS:
    case InstructionMnem::SCAS:
    case InstructionMnem::CMPS:
    case InstructionMnem::OUTS:
        if (ins.operationSize == 2)
            res += "W";
        else
            res += "D";
        break;
    case InstructionMnem::PUSHA:
    case InstructionMnem::PUSHF:
    case InstructionMnem::POPA:
    case InstructionMnem::POPF:
    case InstructionMnem::IRET:
        if (ins.operandSize == 4)
            res += "D";
        break;
    case InstructionMnem::MUL:
    case InstructionMnem::IMUL:
    case InstructionMnem::DIV:
    case InstructionMnem::IDIV:
        if (ins.numOperands == 1 && EAIsMemory(ins.ea[0].type)) {
            memSize = ins.operandSize;
        }
    default:
        break;
    }

    if (ins.numOperands == 2) {
        std::uint8_t opSize = 0;
        for (int i = 0; i < ins.numOperands; ++i) {
            if (EAIsMemory(ins.ea[i].type))
                memSize = ins.operandSize;
            else
                opSize = ResultSizeFromOpmode(ins.instruction->operands[i], ins.operationSize, ins.instruction->mnemonic);
        }

        if (memSize == opSize)
            memSize = 0;
    }

    for (int i = 0; i < ins.numOperands; ++i)
        res += std::format("{}{}", i ? ", " : "\t", DecodedEAInfo { ins.ea[i], addr + ins.numInstructionBytes, ins.prefixes, memSize });
    return res;
}

std::string FormatDecodedInstructionFull(const InstructionDecodeResult& ins, const Address& addr)
{
    const int maxBytesPerLine = 8; // Makes the instruction start on a new tab position
    std::string res;

    for (int i = 0; i < ins.numInstructionBytes; ++i) {
        if (i % maxBytesPerLine == 0) {
            if (i)
                res += '\n';
            res += std::format("{:22} ", addr + i);
        }
        res += std::format("{:02X}", ins.instructionBytes[i]);
    }
    if (int i = ins.numInstructionBytes % maxBytesPerLine; i != 0) {
        for (; i < maxBytesPerLine; ++i)
            res += "  ";
    }
    res += " ";
    return res + FormatDecodedInstruction(ins, addr);
}
