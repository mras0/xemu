#include "cpu.h"
#include "cpu_flags.h"
#include "cpu_exception.h"
#include "system_bus.h"
#include <print>
#include <cstring>

constexpr uint32_t DEFAULT_EFLAGS_RESULT_MASK = EFLAGS_MASK_OF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_AF | EFLAGS_MASK_PF | EFLAGS_MASK_CF;
constexpr uint32_t VALID_CR_MASK = 1 << 0 | 1 << 2 | 1 << 3 | 1 << 4 | 1 << 8;

#define HANDLE_ADD_CARRY() carry = (l & r) | ((l | r) & ~result)
#define HANDLE_SUB_CARRY() carry = (~l & r) | (~(l ^ r) & result)

#define IP_PREFIX() std::format("{} - ", currentIp())

#define THROW_UD(...) do { \
        if (cpuModel_ <= CPUModel::i8086) \
            throw std::runtime_error { IP_PREFIX()  + std::format("TODO:" __VA_ARGS__) }; \
        if (exceptionTraceMask_ & (1 << CPUExceptionNumber::InvalidOpcode)) { \
            std::print("{}", IP_PREFIX()); \
            std::println(__VA_ARGS__); \
        } \
        throw CPUException{CPUExceptionNumber::InvalidOpcode}; \
    } while (false)

#define THROW_GP(...) do { \
        assert(cpuModel_ >= CPUModel::i80286); \
        if (exceptionTraceMask_ & (1 << CPUExceptionNumber::GeneralProtection)) { \
            std::print("{}", IP_PREFIX()); \
            std::println(__VA_ARGS__); \
        } \
        throw CPUException { CPUExceptionNumber::GeneralProtection }; \
    } while (false)

static void SetFlag(uint32_t& flags, uint32_t mask, bool set)
{
    if (set)
        flags |= mask;
    else
        flags &= ~mask;
}

std::string FormatCPUFlags(uint32_t flags)
{
    std::string res = std::format("{:08x} ", flags);
#define FB(b) res += flags & EFLAGS_MASK_##b##F ? #b : "-"
    FB(O);
    FB(D);
    FB(I);
    FB(T);
    FB(S);
    FB(Z);
    FB(A);
    FB(P);
    FB(C);
#undef FB
    return res;
}

void ShowCPUState(const CPUState& state)
{
    const Reg regOrder[] = {
        REG_AX,
        REG_BX,
        REG_CX,
        REG_DX,
        REG_SI,
        REG_DI,
        REG_SP,
        REG_BP,
    };
    const SReg sregOrder[] = {
        SREG_CS,
        SREG_SS,
        SREG_DS,
        SREG_ES,
        SREG_FS,
        SREG_GS,
    };
    for (int i = 0; i < 8; ++i) {
        auto r = regOrder[i];
        std::print("{}={:08X}{}", Reg32Text[r], state.regs_[r] & 0xffffffff, i == 7 ? '\n' : ' ');
    }
    for (int i = 0; i < 6; ++i) {
        auto r = sregOrder[i];
        std::print("{}={:04X} ", SRegText[r], state.sregs_[r]);
    }
    std::print("flags={} {}-bit", FormatCPUFlags(state.flags_), state.defaultOperandSize() * 8);
    if (state.protectedMode())
        std::print(" CPL={}", state.cpl());
    std::println("");
}

template <>
struct std::formatter<SegmentedAddress> : std::formatter<const char*> {
    auto format(const SegmentedAddress& sa, std::format_context& ctx) const
    {
        return std::formatter<const char*>::format(std::format("{}:{:08X}", SRegText[sa.sreg], sa.offset).c_str(), ctx);
    }
};


static std::uint8_t GetU8L(const std::uint64_t val)
{
    return static_cast<uint8_t>(val);
}

static std::uint8_t GetU8H(const std::uint64_t val)
{
    return static_cast<uint8_t>(val >> 8);
}

static std::uint16_t GetU16(const std::uint64_t val)
{
    return static_cast<uint16_t>(val);
}

static std::uint32_t GetU32(const std::uint64_t val)
{
    return static_cast<uint32_t>(val);
}

static std::uint64_t Get(const std::uint64_t val, std::uint8_t opSize)
{
    switch (opSize) {
    case 1:
        return GetU8L(val);
    case 2:
        return GetU16(val);
    case 4:
        return GetU32(val);
    }
    throw std::runtime_error { std::format("TODO: Get(0x{:X}, opSize={})", val, opSize) };
}

static void UpdateU8L(std::uint64_t& reg, std::uint64_t value)
{
    reg = (reg & ~0xffULL) | (value & 0xff);
}

static void UpdateU8H(std::uint64_t& reg, std::uint64_t value)
{
    reg = (reg & ~0xff00ULL) | ((value & 0xff) << 8);
}

static void UpdateU16(std::uint64_t& reg, std::uint64_t value)
{
    reg = (reg & ~0xffffULL) | (value & 0xffff);
}

static void UpdateU32(std::uint64_t& reg, std::uint64_t value)
{
    reg = (reg & ~0xffffffffULL) | (value & 0xffffffff);
}

static void Update(std::uint64_t& reg, std::uint64_t val, std::uint8_t opSize)
{
    switch (opSize) {
    case 1:
        UpdateU8L(reg, val);
        break;
    case 2:
        UpdateU16(reg, val);
        break;
    case 4:
        UpdateU32(reg, val);
        break;
    default:
        throw std::runtime_error { std::format("TODO: Update(..., 0x{:X}, opSize={})", val, opSize) };
    }
}

static std::uint64_t AddReg(std::uint64_t& reg, std::int32_t addend, std::uint8_t opSize)
{
    std::uint64_t res;
    switch (opSize) {
    case 1:
        res = GetU8L(reg) + addend;
        UpdateU8L(reg, res);
        res &= 0xff;
        break;
    case 2:
        res = GetU16(reg) + addend;
        UpdateU16(reg, res);
        res &= 0xffff;
        break;
    case 4:
        res = GetU32(reg) + addend;
        UpdateU32(reg, res);
        res &= 0xffffffff;
        break;
    default:
        throw std::runtime_error { std::format("TODO: AddReg with opSize = {}", opSize) };
    }
    return res;
}

void CPU::reset()
{
    std::memset(static_cast<CPUState*>(this), 0, sizeof(CPUState));
    currentInstruction = {};
    instructionsExecuted_ = 0;
    halted_ = false;

    setFlags(0);
    for (int sr = SREG_ES; sr <= SREG_GS; ++sr) {
        if (sr != SREG_CS)
            loadSreg(SReg(sr), 0);
    }

    if (cpuModel_ <= CPUModel::i8086) {
        sregs_[SREG_CS] = 0xffff;
        ip_ = 0;
    } else {
        // Note really correct, but close enough (i386 has CS.base=0xFFFF0000 and IP = 0xFFF0). A31-A20 remains high until first inter segment jump
        loadSreg(SREG_CS, 0xF000);
        ip_ = 0xFFF0;
    }

    if (cpuModel_ == CPUModel::i80386) {
        regs_[REG_DX] = 3 << 8 | 8; // Stepping in DL (8 = D1/D2)
    }

    idt_.base = 0;
    idt_.limit = 0x3ff;
}

CPUInfo CPU::cpuInfo() const
{
    return CPUInfo { cpuModel_, defaultOperandSize() };
}

constexpr uint32_t PT32_MASK_P = 1 << 0; // Present
constexpr uint32_t PT32_MASK_W = 1 << 1; // Writable
constexpr uint32_t PT32_MASK_U = 1 << 2; // User accessible (otherwise only for supervisor)
constexpr uint32_t PT32_MASK_A = 1 << 5; // Accessed
constexpr uint32_t PT32_MASK_D = 1 << 6; // Dirty (not for PDE)

constexpr uint32_t PDE32_MASK_PS = 1 << 7;
constexpr uint32_t PT32_MASK_ADDR = 0xfffffc00; // Bit 31-12

constexpr uint32_t PL_MASK_P = 1 << 0; // 1 if the fault was caused by a protection violation
constexpr uint32_t PL_MASK_W = 1 << 1; // 1 if the access was a write
constexpr uint32_t PL_MASK_U = 1 << 2; // 1 if the access is by a user process
constexpr uint32_t PL_MASK_I = 1 << 3; // 1 for instruction fetches

constexpr uint32_t PL_FLAG_MASK_ERRS = 15;
constexpr uint32_t PL_FLAG_MASK_PEEK = 1 << 4;

std::string PageCommonText(uint32_t p)
{
    std::string res = std::format("{:08X}", p);
    auto add = [&](const char* text) {
        if (!res.empty())
            res += ", ";
        res += text;
    };
    if (!(p & PT32_MASK_P)) {
        add("Not present");
        return res;
    }
    add("Present");
    if (p & PT32_MASK_W)
        add("Writable");
    if (p & PT32_MASK_U)
        add("User");
    if (p & PT32_MASK_A)
        add("Accessed");
    return res;
}

std::string PdeText(uint32_t pde)
{
    return PageCommonText(pde);
}

std::string PteText(uint32_t pte)
{
    auto res = PageCommonText(pte);
    if (pte & PT32_MASK_D)
        res += ", Dirty";
    return res;
}

std::uint64_t CPU::pageLookup(std::uint64_t linearAddress, std::uint32_t lookupFlags)
{
    assert(!(lookupFlags & ~(PL_MASK_W | PL_MASK_I | PL_FLAG_MASK_PEEK)));
    bool checkWrite = true;
    auto err = lookupFlags & PL_FLAG_MASK_ERRS;
    if (cpl() == 3)
        err |= PL_MASK_U;
    else if (cpl() == 0 && !(cregs_[0] & CR0_MASK_WP))
        checkWrite = false;

    const auto pdeAddr = cregs_[3] + ((linearAddress >> 22) & 1023) * 4;
    const auto pde = static_cast<uint32_t>(readMemPhysical(pdeAddr, 4));
    if (!(pde & PT32_MASK_P)) {
        std::print("LinearAddress {:08X} --> PDE not present: {}\n", linearAddress, PdeText(pde));
    PageFault:
        cregs_[2] = linearAddress;
        throw CPUException { CPUExceptionNumber::PageFault, err };
    }

    if (pde & PDE32_MASK_PS)
        throw std::runtime_error { std::format("TODO: LinearAddress {:08X} --> {:08X} -- 4MB page", linearAddress, pde) };

    // Only check permissions on PDE after checking if PTE is present
    const auto pteAddr = (pde & PT32_MASK_ADDR) + ((linearAddress >> 12) & 1023) * 4;
    const auto pte = static_cast<uint32_t>(readMemPhysical(pteAddr, 4));
    if (!(pte & PT32_MASK_P)) {
        std::print("LinearAddress {:08X} --> PTE not present: {}\n", linearAddress, PteText(pte));
        goto PageFault;
    }

    // Mark as present in error code
    err |= PL_MASK_P;

    if ((err & PL_MASK_U) && !(pde & PT32_MASK_U)) {
        std::print("LinearAddress {:08X} --> PDE access violation (user): {}\n", linearAddress, PdeText(pde));
        goto PageFault;
    }

    if (checkWrite && (lookupFlags & PL_MASK_W) && !(pde & PT32_MASK_W)) {
        std::print("LinearAddress {:08X} --> PDE access violation (not writeable): {}\n", linearAddress, PdeText(pde));
        goto PageFault;
    }

    if ((err & PL_MASK_U) && !(pte & PT32_MASK_U)) {
        std::print("LinearAddress {:08X} --> PTE access violation (user): {}\n", linearAddress, PteText(pte));
        goto PageFault;
    }

    if (checkWrite && (lookupFlags & PL_MASK_W) && !(pte & PT32_MASK_W)) {
        std::print("LinearAddress {:08X} --> PTE access violation (not writable): {}\n", linearAddress, PteText(pte));
        goto PageFault;
    }

    if (!(lookupFlags & PL_FLAG_MASK_PEEK)) {
        if (!(pde & PT32_MASK_A))
            writeMemPhysical(pdeAddr, pde | PT32_MASK_A, 4);

        const uint32_t fl = PT32_MASK_A | (lookupFlags & PL_MASK_W ? PT32_MASK_D : (pte & PT32_MASK_D));
        if ((pte & (PT32_MASK_A | PT32_MASK_D)) != fl)
            writeMemPhysical(pteAddr, pte | fl, 4);
    }

    return (pte & PT32_MASK_ADDR) + (linearAddress & 4095);
}

std::uint8_t CPU::readCodeByte(std::uint64_t offset, bool peek)
{
    const auto addr = SegmentedAddress { SREG_CS, offset };
    uint64_t physAddress;
    if (pagingEnabled()) {
        try {
            const auto flags = peek ? PL_FLAG_MASK_PEEK : PL_MASK_I;
            physAddress = pageLookup(toLinearAddress(addr, 1), flags);
        } catch (const CPUException& e) {
            assert(e.exceptionNo() == CPUExceptionNumber::PageFault);
            (void)e;
            return 0xCC;
        }
    } else {
        physAddress = toLinearAddress(addr, 1);
    }
    if (peek)
        return bus_.peekU8(physAddress);
    else
        return static_cast<uint8_t>(readMemPhysical(physAddress, 1));
}

std::uint64_t CPU::toLinearAddress(const SegmentedAddress& address, std::uint8_t accessSize) const
{
    if (cpuModel_ <= CPUModel::i8086)
        return (sregs_[address.sreg] * 16 + address.offset) & 0xfffff;

    const auto& desc = sdesc_[address.sreg];
    if ((desc.access & (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S)) != (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S))
        throw std::runtime_error { std::format("Segment {} descriptor invalid {}\n", SRegText[address.sreg], desc) };

    if (address.offset + accessSize - 1 > desc.limit) {
        const auto exceptionNo = address.sreg == SREG_SS ? CPUExceptionNumber::StackSegmentFault : CPUExceptionNumber::GeneralProtection;
        if (exceptionTraceMask_ & (1 << exceptionNo))
            std::print("Access of 0x{:04X}:0x{:08X} through {} outside limit {}\n", sregs_[address.sreg], address.offset, SRegText[address.sreg], desc);
        throw CPUException { exceptionNo };
    }

    return desc.base + (address.offset & 0xffffffff);
}

std::uint64_t CPU::toPhysicalAddress(const SegmentedAddress& address, std::uint8_t accessSize, std::uint32_t lookupFlags)
{
    const auto linearAddress = toLinearAddress(address, accessSize);
    return pagingEnabled() ? pageLookup(linearAddress, lookupFlags) : linearAddress;
}

std::uint64_t CPU::readMemPhysical(std::uint64_t physicalAddress, std::uint8_t size)
{
    std::uint64_t value;
    switch (size) {
    case 1:
        value = bus_.readU8(physicalAddress);
        break;
    case 2:
        value = bus_.readU16(physicalAddress);
        break;
    case 4:
        value = bus_.readU32(physicalAddress);
        break;
    case 8:
        value = bus_.readU64(physicalAddress);
        break;
    default:
        throw std::runtime_error { std::format("TODO: Read from 0x{:X} size {}", physicalAddress, size) };
    }
    return value;
}

std::uint64_t CPU::readMemLinear(std::uint64_t linearAddress, std::uint8_t size)
{
    if (pagingEnabled())
        return readMemPhysical(pageLookup(linearAddress, 0), size);
    else
        return readMemPhysical(linearAddress, size);
}

uint64_t CPU::verifyAddress(const SegmentedAddress& addr, uint8_t size, bool forWrite)
{
    auto nextVa = &verifiedAddresses_[0];
    for (auto& va : verifiedAddresses_) {
        if (!va.valid) {
            nextVa = &va;
            break;
        }
        if (va.addr == addr && va.size >= size && (va.forWrite || !forWrite)) {
            std::println("Reusing verified address {} -> {}!", addr, va.physicalAddress);
            return va.physicalAddress;
        }
    }

    if (forWrite && protectedMode()) {
        const auto& desc = sdesc_[addr.sreg];
        if ((desc.access & (SD_ACCESS_MASK_E | SD_ACCESS_MASK_RW)) != SD_ACCESS_MASK_RW) {
            std::print("TODO: #GP fault for write to 0x{:04X}:0x{:08X} size {} through {} {}\n", sregs_[addr.sreg], addr.offset, size, SRegText[addr.sreg], desc);
            throw CPUException { CPUExceptionNumber::GeneralProtection };
        }
    }

    nextVa->physicalAddress = toPhysicalAddress(addr, size, forWrite ? PL_MASK_W : 0);
    nextVa->addr = addr;
    nextVa->size = size;
    nextVa->valid = true;
    nextVa->forWrite = forWrite;
    return nextVa->physicalAddress;
}

std::uint64_t CPU::readMem(const SegmentedAddress& address, std::uint8_t size)
{
    if (cpuModel_ <= CPUModel::i8086) {
        const auto phys0 = (sregs_[address.sreg] * 16 + (address.offset & 0xffff)) & 0xfffff;
        if (size == 1)
            return bus_.readU8(phys0);
        assert(size <= 2);
        if (cpuModel_ == CPUModel::i8086 && (address.offset & 1) == 0) // i8086 can read a word from an even address
            return bus_.readU16(phys0);
        const uint32_t res = bus_.readU8(phys0);
        return res | bus_.readU8((sregs_[address.sreg] * 16 + ((address.offset + 1) & 0xffff)) & 0xfffff) << 8;
    }

    return readMemPhysical(verifyAddress(address, size, false), size);
}

Address CPU::readFarPtr(const DecodedEA& addrEa)
{
    if (addrEa.type == DecodedEAType::rm16 || addrEa.type == DecodedEAType::rm32) {
        auto addr = calcAddress(addrEa);
        const auto offset = readMem(addr, currentInstruction.operandSize);
        addr.offset += currentInstruction.operandSize;
        addr.offset &= currentInstruction.addressMask();
        const auto seg = static_cast<uint16_t>(readMem(addr, 2));
        return Address { seg, offset, currentInstruction.addressSize };
    } else {
        THROW_UD("{} with {}", currentInstruction.mnemoic, addrEa);
    }
}

void CPU::writeMem(const SegmentedAddress& address, std::uint64_t value, std::uint8_t size)
{
    if (cpuModel_ <= CPUModel::i8086) {
        const auto phys0 = (sregs_[address.sreg] * 16 + (address.offset & 0xffff)) & 0xfffff;
        if (size == 1) {
            bus_.writeU8(phys0, static_cast<uint8_t>(value));
            return;
        }
        assert(size <= 2);
        if (cpuModel_ == CPUModel::i8086 && (address.offset & 1) == 0) {
            bus_.writeU16(phys0, static_cast<uint16_t>(value));
            return;
        }
        bus_.writeU8(phys0, static_cast<uint8_t>(value));
        bus_.writeU8((sregs_[address.sreg] * 16 + ((address.offset + 1) & 0xffff)) & 0xfffff, static_cast<uint8_t>(value >> 8));
        return;
    }

    writeMemPhysical(verifyAddress(address, size, true), value, size);
}

void CPU::writeMemPhysical(std::uint64_t address, std::uint64_t value, std::uint8_t size)
{
    switch (size) {
    case 1:
        bus_.writeU8(address, static_cast<std::uint8_t>(value));
        break;
    case 2:
        bus_.writeU16(address, static_cast<std::uint16_t>(value));
        break;
    case 4:
        bus_.writeU32(address, static_cast<std::uint32_t>(value));
        break;
    default:
        throw std::runtime_error { std::format("TODO: Write to {:X} size {} value {:0{}X}", address, size, Get(value, size), 2 * size) };
    }
}

SegmentedAddress CPU::calcAddressNoMask(const DecodedEA& ea) const
{
    std::uint64_t offset = 0;
    SReg segment = SREG_DS;

    if (ea.type == DecodedEAType::mem16) {
        offset = ea.address & 0xffff;
    } else if (ea.type == DecodedEAType::mem32) {
        offset = ea.address & 0xffffffff;
    } else {
        assert(ea.type == DecodedEAType::rm16 || ea.type == DecodedEAType::rm32);
        const auto mod = ModrmMod(ea.rm);
        const auto rm = ModrmRm(ea.rm);
        assert(mod != 0b11);

        if (ea.type == DecodedEAType::rm16) {
            if (mod == 0b00 && rm == 0b110) {
                offset = ea.disp & 0xffff;
            } else {
                // const char* const Rm16Text[8] = { "BX+SI", "BX+DI", "BP+SI", "BP+DI", "SI", "DI", "BP", "BX" };
                constexpr Reg baseReg[8] = { REG_BX, REG_BX, REG_BP, REG_BP, REG_SI, REG_DI, REG_BP, REG_BX };
                constexpr Reg indexReg[4] = { REG_SI, REG_DI, REG_SI, REG_DI };
                if (baseReg[rm] == REG_BP)
                    segment = SREG_SS;
                offset = GetU16(regs_[baseReg[rm]]);
                if (rm < 4)
                    offset += GetU16(regs_[indexReg[rm]]);
                if (mod == 0b01)
                    offset += static_cast<int8_t>(ea.disp & 0xff);
                else if (mod == 0b10)
                    offset += static_cast<int16_t>(ea.disp & 0xffff);
            }
        } else if (ea.type == DecodedEAType::rm32) {
            if (rm == REG_SP) {
                assert(Modrm32HasSib(ea.rm));
                // SIB
                const auto scale = (ea.sib >> 6) & 3;
                const auto index = (ea.sib >> 3) & 7;
                const auto base = ea.sib & 7;

                uint64_t indexVal = 0;
                if (index != REG_SP) {
                    indexVal = regs_[index] << scale;
                } else if (scale && cpuModel_ < CPUModel::i80586 && !(base == REG_BP && mod == 0b00)) {
                    // Undocumented 80386/80486 behavior - ss > 0 and "no index" => base is scaled by scale
                    // But not when there is no base register (disp32 only)
                    indexVal = (regs_[base] << scale) - regs_[base];
                }
                if (base == REG_BP && mod == 0b00) {
                    // disp32 rather than base register
                    offset = ea.disp + indexVal;
                } else {
                    if (base == REG_BP || base == REG_SP)
                        segment = SREG_SS;
                    offset = regs_[base] + indexVal;
                }
            } else if (rm == REG_BP) {
                assert(!Modrm32HasSib(ea.rm));
                if (mod != 0b00) {
                    offset = regs_[REG_BP];
                    segment = SREG_SS;
                } else {
                    offset = ea.disp; // [disp32]
                }
            } else {
                assert(!Modrm32HasSib(ea.rm));
                offset = regs_[rm];
            }
            if (mod == 0b01)
                offset += static_cast<int8_t>(ea.disp & 0xff);
            else if (mod == 0b10)
                offset += static_cast<int32_t>(ea.disp & 0xffffffff);
            offset &= 0xffffffff;
        } else {
            throw std::runtime_error { "calcAddress " + std::string(DecodedEATypeText(ea.type)) };
        }

    }

    if (currentInstruction.prefixes & PREFIX_SEG_MASK) {
        segment = static_cast<SReg>(((currentInstruction.prefixes & PREFIX_SEG_MASK) >> PREFIX_SEG_SHIFT) - 1);
    }
    return SegmentedAddress { segment, offset };
}

SegmentedAddress CPU::calcAddress(const DecodedEA& ea) const
{
    auto sa = calcAddressNoMask(ea);
    if (ea.type == DecodedEAType::rm16)
        sa.offset &= 0xffff;
    return sa;
}

void CPU::checkSreg(std::uint8_t regNum)
{
    assert(currentInstruction.operationSize == 2 || currentInstruction.opcode == 0x8C);
    if (regNum >= 6) {
        THROW_UD("Invalid segment register {}", regNum);
    }
}

std::uint64_t CPU::readEA(int index)
{
    assert(index >= 0 && index < currentInstruction.numOperands);
    const auto& ea = currentInstruction.ea[index];
    switch (ea.type) {
    case DecodedEAType::reg8: {
        assert(ea.regNum < 8);
        auto& reg = regs_[ea.regNum & 3];
        if (ea.regNum & 4)
            return GetU8H(reg);
        else
            return GetU8L(reg);
    }
    case DecodedEAType::reg16:
        assert(ea.regNum < 8);
        return GetU16(regs_[ea.regNum]);
    case DecodedEAType::reg32:
        assert(ea.regNum < 8);
        return GetU32(regs_[ea.regNum]);
    case DecodedEAType::sreg:
        checkSreg(ea.regNum);
        return sregs_[ea.regNum];
    case DecodedEAType::creg:
        assert(ea.regNum < 8);
        if (!(VALID_CR_MASK & (1U << ea.regNum)))
            THROW_UD("Warning: Read from Invalid CR{}", ea.regNum);
        return cregs_[ea.regNum];
    case DecodedEAType::imm8:
        return SignExtend(ea.immediate, 1);
    case DecodedEAType::imm16:
        return SignExtend(ea.immediate, 2);
    case DecodedEAType::imm32:
        return SignExtend(ea.immediate, 4);
    case DecodedEAType::rm16:
    case DecodedEAType::rm32:
    case DecodedEAType::mem16:
    case DecodedEAType::mem32:
        return readMem(calcAddress(ea), currentInstruction.operandSize);
    default:
        throw std::runtime_error { std::string("TODO: readEA ") + DecodedEATypeText(ea.type) };
    }
}

void CPU::writeEA(int index, std::uint64_t value)
{
    assert(index >= 0 && index < currentInstruction.numOperands);
    const auto& ea = currentInstruction.ea[index];
    assert(currentInstruction.operationSize);

    switch (ea.type) {
    case DecodedEAType::reg8: {
        assert(currentInstruction.operationSize == 1);
        assert(ea.regNum < 8);
        auto& reg = regs_[ea.regNum & 3];
        if (ea.regNum & 4)
            UpdateU8H(reg, value);
        else
            UpdateU8L(reg, value);
        break;
    }
    case DecodedEAType::reg16: {
        assert(currentInstruction.operationSize == 2);
        assert(ea.regNum < 8);
        UpdateU16(regs_[ea.regNum], value);
        break;
    }
    case DecodedEAType::reg32: {
        assert(currentInstruction.operationSize == 4);
        assert(ea.regNum < 8);
        UpdateU32(regs_[ea.regNum], value);
        break;
    }
    case DecodedEAType::sreg: {
        checkSreg(ea.regNum);
        if (ea.regNum == SREG_CS) // Can't write directly to CS (TODO: This is a 186+ thing)
            THROW_UD("Write to CS");
        loadSreg(static_cast<SReg>(ea.regNum), value & 0xffff);
        break;
    }
    case DecodedEAType::creg:
        assert(currentInstruction.operationSize == 4);
        assert(ea.regNum < 8);
        //std::print("Write to CR{} value=0x{:08X}\n", ea.regNum, value);
        if (!(VALID_CR_MASK & (1U << ea.regNum)))
            THROW_UD("Warning: Write to Invalid CR{} value=0x{:08X}", ea.regNum, value);
        if (ea.regNum == 0 && (value & CR0_MASK_PG) && !(value & CR0_MASK_PE))
            throw std::runtime_error { "Cannot enable paging w/o PE" }; // Should be a GPE
        cregs_[ea.regNum] = value;
        break;
    case DecodedEAType::rm16:
    case DecodedEAType::rm32:
    case DecodedEAType::mem16:
    case DecodedEAType::mem32:
        writeMem(calcAddress(ea), value, currentInstruction.operandSize);
        break;
    default:
        throw std::runtime_error { std::string("TODO: writeEA ") + DecodedEATypeText(ea.type) + " value " + HexString(value, currentInstruction.operationSize * 2) };
    }
}

void CPU::setFlags(std::uint32_t value)
{
    flags_ = value;
    if (cpuModel_ < CPUModel::i80386) {
        flags_ &= 0xffff - 0x28;
        flags_ |= 0xf002;
    } else {
        flags_ |= 0xfffc0002;
        flags_ &= ~(1 << 3 | 1 << 5 | 1 << 15);
        //flags_ &= ~0x3FC08028;
    }
}

void CPU::updateFlags(std::uint64_t value, std::uint64_t carry, std::uint32_t flagsMask)
{
    uint64_t msbMask;
    switch (currentInstruction.operationSize) {
    case 1:
        value &= 0xff;
        msbMask = 0x80;
        break;
    case 2:
        value &= 0xffff;
        msbMask = 0x8000;
        break;
    case 4:
        value &= 0xffffffff;
        msbMask = 0x80000000;
        break;
    default:
        assert(false);
        throw std::runtime_error { "Invalid result size " + std::to_string(currentInstruction.operationSize) };
    }

    std::uint32_t flags = 0;
    if (carry & msbMask)
        flags |= EFLAGS_MASK_CF;
    if ((flagsMask & EFLAGS_MASK_PF) && Parity(static_cast<uint8_t>(value)))
        flags |= EFLAGS_MASK_PF;
    if (carry & (1 << 3))
        flags |= EFLAGS_MASK_AF;
    if (!value)
        flags |= EFLAGS_MASK_ZF;
    if (value & msbMask)
        flags |= EFLAGS_MASK_SF;
    // TODO: Check if overflow is correct
    if (((carry << 1) ^ carry) & msbMask)
        flags |= EFLAGS_MASK_OF;
    flags_ = (flags_ & ~flagsMask) | (flags & flagsMask);
}

std::uint64_t CPU::readStack(std::int32_t itemOffset)
{
    auto sp = currentSp();
    sp.offset += itemOffset * currentInstruction.operandSize;
    sp.offset &= stackMask();
    return readMem(sp, currentInstruction.operandSize);
}

void CPU::writeStack(std::int32_t itemOffset, std::uint64_t value)
{
    auto sp = currentSp();
    sp.offset -= (1 + itemOffset) * currentInstruction.operandSize;
    sp.offset &= stackMask();
    writeMem(sp, value, currentInstruction.operandSize);
}

void CPU::updateSp(std::int32_t itemCount)
{
    AddReg(regs_[REG_SP], currentInstruction.operandSize * itemCount, stackSize());
}

void CPU::push(std::uint64_t value, std::uint8_t size)
{
    assert(size == 2 || size == 4);
    AddReg(regs_[REG_SP], -size, stackSize());
    writeMem(currentSp(), value, size);
}

std::uint64_t CPU::pop(std::uint8_t size)
{
    assert(size == 2 || size == 4);
    const auto res = readMem(currentSp(), size);
    AddReg(regs_[REG_SP], size, stackSize());
    return res;
}


void CPU::showState(const CPUState& state, const uint8_t* instructionBytes)
{
    ShowCPUState(state);
    auto pc = Address { state.sregs_[SREG_CS], state.ip_, state.defaultOperandSize() };
    try {
        int64_t offset = 0;
        auto fetch = [&]() {
            if (instructionBytes)
                return instructionBytes[offset++];
            else
                return readCodeByte(state.ip_ + (offset++), true);
        };
        const auto res = Decode(CPUInfo { cpuModel_, state.defaultOperandSize() }, fetch);
        std::print("{}\n", FormatDecodedInstructionFull(res, pc));
    } catch (const std::exception& e) {
        std::print("{} {}\n", pc, e.what());
    }
}

void CPU::trace()
{
    showState(*this, nullptr);
}

void CPU::clearHistory()
{
    instructionsExecuted_ = 0;
}

void CPU::showHistory(size_t max)
{
    if (max > instructionsExecuted_)
        max = instructionsExecuted_;

    for (size_t i = instructionsExecuted_ - max; i < instructionsExecuted_; ++i) {
        auto& history = history_[i % MaxHistory];
        showState(history.state, history.instructionBytes);
        if (history.exception != ExceptionNone) {
            std::println("*** {} ***", FormatExceptionNumber(history.exception));
        }
    }
}

Address CPU::currentIp() const
{
    return Address { sregs_[SREG_CS], currentIp_, defaultOperandSize() };
}

SegmentedAddress CPU::currentSp() const
{
    return SegmentedAddress { SREG_SS, regs_[REG_SP] & stackMask() };
}

int CPU::lastExceptionNo() const
{
    if (!instructionsExecuted_)
        return -1;
    return history_[(instructionsExecuted_ - 1) % MaxHistory].exception;
}

void CPU::step()
{
    // XXX: Reconsider
    // TODO: Double fault
    if ((flags_ & EFLAGS_MASK_IF) && intFunc_) {
        int interrupt = intFunc_();
        if (interrupt >= 0) {
            halted_ = false;
            doInterrupt(static_cast<std::uint8_t>(interrupt), true);
        }
    }

    if (halted_) {
        bus_.addCycles(1);
        return;
    }

    auto& history = history_[instructionsExecuted_++ % MaxHistory];
    std::memcpy(&history.state, &static_cast<const CPUState&>(*this), sizeof(CPUState));
    history.exception = ExceptionNone;
    currentInstruction.numInstructionBytes = 0;
    currentIp_ = ip_;
    try {
        try {
            doStep();
            memcpy(history.instructionBytes, currentInstruction.instructionBytes, currentInstruction.numInstructionBytes);
        } catch (...) {
            // Move back....
            ip_ = currentIp_;
            memcpy(history.instructionBytes, currentInstruction.instructionBytes, currentInstruction.numInstructionBytes);
            throw;
        }
    } catch (const CPUException& e) {
        const auto exceptionNo = static_cast<std::uint8_t>(e.exceptionNo());

        if ((1 << exceptionNo) & exceptionTraceMask_)
            std::print("{} - {}\n", currentIp(), e.what());

        if (exceptionNo == CPUExceptionNumber::DivisionError) {
            if (cpuModel_ == CPUModel::i8088) {
                // On the 8088 specifically, the return address pushed to the stack on divide exception is the address of the next instruction. (From SingleStepTests)
                ip_ = (ip_ + currentInstruction.numInstructionBytes) & 0xffff;
            }
        }
        doInterrupt(exceptionNo, true);
        if (e.hasErrorCode() && protectedMode())
            push(e.errorCode(), 4);
    }
}

constexpr uint32_t TSS32_ESP0_OFFSET = 0x04;
constexpr uint32_t TSS32_SS0_OFFSET = 0x08;

std::uint64_t CPU::tssAddress(std::uint32_t limitCheck)
{
    // TODO: Ignore busy flag
    if ((task_.access & (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S | SD_ACCESS_MASK_TYPE)) != (SD_ACCESS_MASK_P | SD_TYPE_TASK32_AVAILABLE))
        throw std::runtime_error { std::format("TODO: Invalid TSS: {}", cpl()) };

    if (limitCheck > task_.limit)
        throw std::runtime_error { std::format("TODO: Outside TSS limit 0x{:X}: {}", limitCheck, cpl()) };

    return task_.base;
}

void CPU::tssSaveStack()
{
    if (cpl() != 0)
        throw std::runtime_error { std::format("TODO: tssSaveStack with newCpl={}", cpl()) };
    const auto tssAddr = tssAddress(TSS32_SS0_OFFSET + 2);
    throw std::runtime_error { std::format("TODO: tssSaveStack with cpl={} tssAddr=0x{:X}", cpl(), tssAddr) };
}

void CPU::tssRestoreStack(std::uint8_t newCpl)
{
    if (newCpl != 0)
        throw std::runtime_error { std::format("TODO: tssRestoreStack with newCpl={}", newCpl) };
    const auto tssAddr = tssAddress(TSS32_SS0_OFFSET + 2);
    const auto ss = static_cast<uint16_t>(readMemLinear(tssAddr + TSS32_SS0_OFFSET, 2));
    const auto sp = static_cast<uint32_t>(readMemLinear(tssAddr + TSS32_ESP0_OFFSET, 4));

    const auto oldSS = sregs_[SREG_SS];
    const auto oldSP = regs_[REG_SP];
    // Lower CPL now (to avoid #GP when restoring SS)
    sregs_[SREG_CS] = (sregs_[SREG_CS] & ~DESC_MASK_DPL) | newCpl;
    loadSreg(SREG_SS, ss);
    regs_[REG_SP] = sp;
    push(oldSS, currentInstruction.operandSize);
    push(oldSP, currentInstruction.operandSize);
}


SegmentDescriptor CPU::readDescriptor(std::uint16_t value)
{
    uint64_t base;
    uint16_t limit;
    if (value & 4) {
        assert((ldt_.access & (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S | SD_ACCESS_MASK_TYPE)) == (SD_ACCESS_MASK_P | SD_TYPE_LDT));
        base = ldt_.base;
        assert(ldt_.limit <= 0xffff);
        limit = static_cast<uint16_t>(ldt_.limit);
    } else {
        base = gdt_.base;
        limit = gdt_.limit;
    }
    const auto ofs = static_cast<uint32_t>(value & ~7);
    if (ofs + 8 > limit)
        throw CPUException { CPUExceptionNumber::GeneralProtection, static_cast<uint32_t>(value & ~DESC_MASK_DPL) };
    return SegmentDescriptor::fromU64(readMemLinear(base + ofs, 8));
}

void CPU::loadSreg(SReg sr, std::uint16_t value)
{
    if (sr == SREG_CS) {
        if (protectedMode())
            throw std::runtime_error { std::format("Setting CS to {:04X} in protected mode with loadSreg", value) };
        sdesc_[SREG_CS].setRealModeCode(value);
    } else if (protectedMode()) {
        const auto rpl = value & DESC_MASK_DPL;
        const std::uint16_t selector = value & ~DESC_MASK_DPL;
        SegmentDescriptor desc = readDescriptor(value);
        const auto dpl = desc.dpl();
        if (sr == SREG_SS) {
            // segment selector is NULL
            if (!value)
                throw CPUException { CPUExceptionNumber::GeneralProtection, 0 };
            // segment selector's RPL != CPL
            // segment is not a writable data segment
            // DPL != CPL
            if (rpl != dpl || (desc.access & (SD_ACCESS_MASK_E | SD_ACCESS_MASK_RW)) != SD_ACCESS_MASK_RW || dpl != cpl())
                throw CPUException { CPUExceptionNumber::GeneralProtection, selector };
            // segment not marked present
            if (!desc.present())
                throw CPUException { CPUExceptionNumber::StackSegmentFault, selector };
        } else if (value) {
            // segment is not a data or readable code segment
            if (!(desc.access & SD_ACCESS_MASK_S) || ((desc.access & SD_ACCESS_MASK_E) && !(desc.access & SD_ACCESS_MASK_RW)))
                throw CPUException { CPUExceptionNumber::GeneralProtection, selector };
            // (segment is a data or nonconforming code segment) AND ((RPL > DPL) or (CPL > DPL))
            if ( (!(desc.access & SD_ACCESS_MASK_E) || (desc.access & (SD_ACCESS_MASK_E | SD_ACCESS_MASK_DC)) == (SD_ACCESS_MASK_E | SD_ACCESS_MASK_DC)) &&
                (rpl > dpl || cpl() > dpl))
                throw CPUException { CPUExceptionNumber::GeneralProtection, selector };
            if (!desc.present())
                throw CPUException { CPUExceptionNumber::SegmentNotPresent, selector };
        }
        //std::print("{} loaded with 0x{:04x} {}\n", SRegText[sr], value, desc);
        sdesc_[sr] = desc;
    } else {
        sdesc_[sr].setRealModeData(value);
    }
    sregs_[sr] = value;
}

void CPU::doControlTransfer(std::uint16_t cs, std::uint64_t ip, ControlTransferType type)
{
    const char* const typeNames[] = { "jump", "call", "interrupt" };
    static_assert(std::size(typeNames) == static_cast<size_t>(ControlTransferType::max));
    const char* const typeName = typeNames[static_cast<size_t>(type)];
    const auto opSize = static_cast<uint8_t>(type == ControlTransferType::interrupt ? (protectedMode() ? 4 : 2) : currentInstruction.operandSize);

    const auto oldCS = sregs_[SREG_CS];
    const auto oldIP = ip_;
    const auto oldFlags = flags_;
    auto saveRegs = [&]() {
        switch (type) {
        case ControlTransferType::jump:
            return;
        case ControlTransferType::interrupt:
            push(oldFlags, opSize);
            [[fallthrough]];
        case ControlTransferType::call:
            push(oldCS, opSize);
            push(oldIP, opSize);
            return;
        default:
            throw std::runtime_error { std::format("TODO: Save registers for ControlTransferType::{}", typeName) };
        }
    };

    if (type == ControlTransferType::interrupt)
        flags_ &= ~EFLAGS_MASK_IF;

    if (!protectedMode()) {
        saveRegs();
        sregs_[SREG_CS] = cs;
        sdesc_[SREG_CS].setRealModeCode(cs);
        ip_ = ip & ipMask();
        return;
    }

    SegmentDescriptor desc = readDescriptor(cs);
    if (!desc.present())
        throw std::runtime_error { "TODO: Code segment not present (raise #NP)" };

    if (desc.access & SD_ACCESS_MASK_S) {
        if (!(desc.access & SD_ACCESS_MASK_E) || !(desc.flags & SD_FLAGS_MASK_DB))
            throw std::runtime_error { std::format("TODO: CS loaded with unsupported descriptor {}", desc) };

        if (type == ControlTransferType::interrupt) {
            const auto newCpl = desc.dpl();
            if (newCpl < cpl()) {
                //std::print("INT: Stack switch CPL {} -> {}\n", cpl(), newCpl);
                tssRestoreStack(newCpl); // Lowers CPL and pushes SS:ESP
            } else {
                //std::print("INT: CPL {} -> {}\n", cpl(), newCpl);
                cs = (cs & ~DESC_MASK_DPL) | newCpl;
            }
        }
    } else {
        if ((desc.access & SD_ACCESS_MASK_TYPE) != SD_TYPE_CALL32)
            throw std::runtime_error { std::format("TODO: CS loaded with unsupported descriptor {}", desc) };

        if (type != ControlTransferType::call)
            throw std::runtime_error { std::format("TODO: Cannot use {} for {}", typeName, desc) };

        const auto codeDesc = readDescriptor(desc.call32.selector);
        if (!codeDesc.present() || !codeDesc.isCodeSegment())
            throw std::runtime_error { std::format("TODO: Unsupported callgate {} referencing {}", desc, codeDesc) };

        if (!(codeDesc.flags & SD_FLAGS_MASK_DB))
            throw std::runtime_error { std::format("TODO: Unsupported (16-bit) callgate {} referencing {}", desc, codeDesc) };

        if (desc.call32.paramCount)
            throw std::runtime_error { std::format("TODO: Unsupported paramater count for callgate {}", desc) };

        const auto newCpl = static_cast<uint8_t>(desc.call32.selector & DESC_MASK_DPL);
        if (newCpl < cpl()) {
            //std::print("Switching stack on call gate transition from {} to {}\n", cpl(), newCpl);
            tssRestoreStack(newCpl); // Also pushes SS:ESP
        }

        //std::print("Using call descriptor:\n{}\n{}\n", desc, codeDesc);

        cs = desc.call32.selector;
        ip = desc.call32.offset();
        desc = codeDesc;
    }

    saveRegs();
    sregs_[SREG_CS] = cs;
    sdesc_[SREG_CS] = desc;
    ip_ = ip & ipMask();
}

void CPU::doNearControlTransfer(ControlTransferType type)
{
    assert(type == ControlTransferType::jump || type == ControlTransferType::call);
    const auto oldIp = ip_;
    const auto& ea = currentInstruction.ea[0];

    if (ea.type == DecodedEAType::rel8)
        ip_ += static_cast<int8_t>(ea.immediate & 0xff);
    else if (ea.type == DecodedEAType::rel16)
        ip_ += static_cast<int16_t>(ea.immediate & 0xffff);
    else if (ea.type == DecodedEAType::rel32)
        ip_ += static_cast<int32_t>(ea.immediate & 0xffffffff);
    else
        ip_ = readEA(0);
    ip_ &= ipMask();
    if (type == ControlTransferType::call)
        push(oldIp, currentInstruction.operandSize);
}

void CPU::doFarJump(std::uint16_t cs, std::uint64_t ip)
{
    if (protectedMode()) {
        SegmentDescriptor desc = readDescriptor(cs);

        if (!desc.present())
            throw std::runtime_error { "TODO: Code segment not present (raise #NP)" };

        if (desc.access & SD_ACCESS_MASK_S) {
            if (!(desc.access & SD_ACCESS_MASK_E) || !(desc.flags & SD_FLAGS_MASK_DB))
                throw std::runtime_error { std::format("TODO: CS loaded with unsupported descriptor {}", desc) };
        } else {
            // Should have gone through doControlTransfer
            throw std::runtime_error { std::format("TODO: doFarJmp, CS loaded with unsupported descriptor {}", desc) };
        }

        sdesc_[SREG_CS] = desc;
    } else {
        sdesc_[SREG_CS].setRealModeCode(cs);
    }
    sregs_[SREG_CS] = cs;
    ip_ = ip & ipMask();
}

void CPU::doInterruptReturn()
{
    const auto ip = readStack(0);
    const auto cs = static_cast<uint16_t>(readStack(1));
    auto flags = static_cast<uint32_t>(readStack(2));
    if (cpuModel_ >= CPUModel::i80286 && ip > sdesc_[SREG_CS].limit)
        THROW_GP("RETN -  return instruction pointer is not within the return code segment limit");
    updateSp(3);

    // TODO: Flags need to be filtered

    if (currentInstruction.operandSize == 2)
        flags |= flags_ & 0xffff0000;

    if (!protectedMode()) {
        setFlags(flags);
        doFarJump(cs, ip);
        return;
    }

    // TODO: Probably need to check CS/SS descriptors before changing registers to allow recovery

    // PROTECTED-MODE-RETURN:
    if ((cs & DESC_MASK_DPL) > cpl()) {
        // RETURN-TO-OUTER-PRIVILEGE-LEVEL

        //std::print("IRET: Stack switch on {} -> {} transition\n", cpl(), (cs & DESC_MASK_DPL));

        // pop before changing privilege level
        const auto sp = pop(currentInstruction.operandSize);
        const auto ss = static_cast<uint16_t>(pop(currentInstruction.operandSize));
        doFarJump(cs, ip);
        regs_[REG_SP] = sp;
        loadSreg(SREG_SS, ss);
        setFlags(flags);

        for (const auto sr : { SREG_ES, SREG_DS, SREG_FS, SREG_GS }) {
            auto& desc = sdesc_[sr];
            // IF (SegmentSelector == NULL) OR (tempDesc(DPL) < CPL AND tempDesc(Type) is (data or non-conforming code)))
            if (desc.dpl() < cpl()) {
                std::memset(&desc, 0, sizeof(desc));
                sregs_[sr] = 0;
            }
        }
    } else {
        // RETURN-TO-SAME-PRIVILEGE-LEVEL
        doFarJump(cs, ip);
        setFlags(flags);
    }
}

void CPU::doInterrupt(std::uint8_t interruptNo, bool hardwareInterrupt)
{
    history_[(instructionsExecuted_ - 1) % MaxHistory].exception = interruptNo | (hardwareInterrupt ? ExceptionHardwareMask : 0);

    if (protectedMode()) {
        if (interruptNo * 8 > idt_.limit) {
            std::print("Interrupt {} over limit {}\n", interruptNo, idt_.limit);
            throw CPUException { CPUExceptionNumber::GeneralProtection };
        }
        const auto desc = readMemLinear(idt_.base + 8 * interruptNo, 8);
        const auto offset = (desc & 0xffff) | ((desc >> 48) << 16);
        const auto selector = static_cast<uint16_t>((desc >> 16) & 0xffff);
        const auto flags = (desc >> 40) & 0xff;
        const auto type = flags & 0xf;
        const auto dpl = (flags >> 5) & 3;
        
        if (!(flags & 0x80))
            throw std::runtime_error { std::format("TODO: Interrupt {} not present in IDT. Desc={:016X} {:04X}:{:08X} flags=0x{:02X}", interruptNo, desc, selector, offset, flags) };

        if (!hardwareInterrupt && dpl < cpl())
            throw std::runtime_error { std::format("TODO: Interrupt {} not allowed at dpl={}. Desc={:016X} {:04X}:{:08X} flags=0x{:02X}", interruptNo, cpl(), desc, selector, offset, flags) };

        if (type != 0xe) // 32-bit interrupt gate
            throw std::runtime_error { std::format("TODO: Interrupt {} in protected mode - Unsupported type. Desc={:016X} {:04X}:{:08X} flags=0x{:02X}", interruptNo, desc, selector, offset, flags) };

        doControlTransfer(selector, offset, ControlTransferType::interrupt);
    } else {
        if (interruptNo * 4 > idt_.limit) {
            std::print("Interrupt {} over limit {}\n", interruptNo, idt_.limit);
            throw CPUException { CPUExceptionNumber::GeneralProtection };
        }
        const auto addr = readMemPhysical(static_cast<uint64_t>(interruptNo) << 2, 4);
        doControlTransfer(static_cast<uint16_t>(addr >> 16), addr & 0xffff, ControlTransferType::interrupt);
    }
}

template <InstructionMnem Ins>
void CPU::doStringInstruction()
{
    const auto opSize = currentInstruction.operationSize;
    const auto addrSize = currentInstruction.addressSize;
    const auto mask = currentInstruction.addressMask();
    const int32_t incr = (flags_ & EFLAGS_MASK_DF) ? -currentInstruction.operationSize : currentInstruction.operationSize;

    // DS may be overriden (ES cannot)
    const auto ds = (currentInstruction.prefixes & PREFIX_SEG_MASK) ? static_cast<SReg>(((currentInstruction.prefixes & PREFIX_SEG_MASK) >> PREFIX_SEG_SHIFT) - 1) : SREG_DS;

    // Need to increment after the operation (in case of #GP SI/DI should not be updated)
    auto incReg = [&](Reg reg) {
        AddReg(regs_[reg], incr, addrSize);
    };

    auto readSI = [&]() {
        return readMem(SegmentedAddress { ds, regs_[REG_SI] & mask }, opSize);
    };
    auto readDI = [&]() {
        return readMem(SegmentedAddress { SREG_ES, regs_[REG_DI] & mask }, opSize);
    };
    auto writeDI = [&](std::uint64_t value) {
        writeMem(SegmentedAddress { SREG_ES, regs_[REG_DI] & mask }, value, opSize);
    };

    constexpr bool isCompare = Ins == InstructionMnem::CMPS || Ins == InstructionMnem::SCAS;

    auto operation = [&]() {
        if constexpr (isCompare) {
            uint64_t l, r, result, carry;
            if constexpr (Ins == InstructionMnem::CMPS) {
                if (cpuModel_ >= CPUModel::i80286) {
                    const auto addr1 = verifyAddress(SegmentedAddress { ds, regs_[REG_SI] & mask }, opSize, false);
                    const auto addr2 = verifyAddress(SegmentedAddress { SREG_ES, regs_[REG_DI] & mask }, opSize, false);
                    l = readMemPhysical(addr1, opSize);
                    r = readMemPhysical(addr2, opSize);
                } else {
                    l = readSI();
                    r = readDI();
                }
            } else {
                l = regs_[REG_AX];
                r = readDI();
            }
            incReg(REG_DI);
            if constexpr (Ins == InstructionMnem::CMPS)
                incReg(REG_SI);
            result = l - r;
            HANDLE_SUB_CARRY();
            updateFlags(result, carry, DEFAULT_EFLAGS_RESULT_MASK);
        } else if constexpr (Ins == InstructionMnem::LODS) {
            Update(regs_[REG_AX], readSI(), opSize);
            incReg(REG_SI);
        } else if constexpr (Ins == InstructionMnem::MOVS) {
            writeDI(readSI());
            incReg(REG_DI);
            incReg(REG_SI);
        } else if constexpr (Ins == InstructionMnem::STOS) {
            writeDI(regs_[REG_AX]);
            incReg(REG_DI);
        } else if constexpr (Ins == InstructionMnem::INS) {
            assert(opSize > 0);
            writeDI(bus_.ioInput(regs_[REG_DX] & 0xFFFF, opSize));
            incReg(REG_DI);
        } else if constexpr (Ins == InstructionMnem::OUTS) {
            assert(opSize > 0);
            //auto addr = toLinearAddress(SegmentedAddress { ds, regs_[REG_SI] & mask }, opSize);
            //addr &= 0xfffff;
            //readMemPhysical(addr, opSize);

            bus_.ioOutput(regs_[REG_DX] & 0xFFFF, static_cast<uint32_t>(readSI()), opSize);
            incReg(REG_SI);
        } else {
            static_assert(false, "Unimplemented string instruction");
        }
    };

    // REPNZ also works for e.g. MOVS
    if (!(currentInstruction.prefixes & PREFIX_REP_MASK)) {
        operation();
        return;
    }

    while (Get(regs_[REG_CX], addrSize) != 0) {
        // TODO: Service interrupts
        operation();
        AddReg(regs_[REG_CX], -1, addrSize);
        if constexpr (isCompare) {
            if (!(flags_ & EFLAGS_MASK_ZF) == !(currentInstruction.prefixes & PREFIX_REPNZ))
                break;
        }
    }
}

template <InstructionMnem Ins>
void CPU::doBitInstruction()
{
    const auto opSize = currentInstruction.operandSize;
    assert(currentInstruction.numOperands == 2);
    assert(opSize == 2 || opSize == 4);
    const bool isMem = EAIsMemory(currentInstruction.ea[0].type);

    auto bitOffset = readEA(1);
    uint64_t val, addr = 0;
    if (isMem) {
        if (currentInstruction.ea[1].type == DecodedEAType::imm8)
            bitOffset %= 8 * currentInstruction.operandSize;

        const auto shift = opSize == 2 ? 4 : 5;
        auto eaAddr = calcAddressNoMask(currentInstruction.ea[0]);
        //std::println("EA={} bitOffset = {} --> {}\n", eaAddr, static_cast<int64_t>(SignExtend(bitOffset, opSize)), (static_cast<int64_t>(SignExtend(bitOffset, opSize)) >> shift) * opSize);
        eaAddr.offset += (static_cast<int64_t>(SignExtend(bitOffset, opSize)) >> shift) * opSize;
        //std::println("--> {}", eaAddr);
        eaAddr.offset &= currentInstruction.addressMask();
        addr = this->verifyAddress(eaAddr, opSize, Ins != InstructionMnem::BT);
        val = readMemPhysical(addr, opSize);
    } else {
        val = readEA(0);
    }
    bitOffset %= 8 * currentInstruction.operandSize;

    const auto mask = uint64_t(1) << bitOffset;
    if (val & mask)
        flags_ |= EFLAGS_MASK_CF;
    else
        flags_ &= ~EFLAGS_MASK_CF;

    const auto rotated = val >> bitOffset | val << (8 * opSize - bitOffset);
    const auto overflow = ((rotated ^ (rotated << 1)) >> (8 * opSize - 1)) & 1;
    if (overflow)
        flags_ |= EFLAGS_MASK_OF;
    else
        flags_ &= ~EFLAGS_MASK_OF;
    
    if constexpr (Ins == InstructionMnem::BTC)
        val ^= mask;
    else if constexpr (Ins == InstructionMnem::BTR)
        val &= ~mask;
    else if constexpr (Ins == InstructionMnem::BTS)
        val |= mask;
    else if constexpr (Ins != InstructionMnem::BT)
        static_assert(false, "Not implemented");

    if constexpr (Ins != InstructionMnem::BT) {
        if (isMem)
            writeMemPhysical(addr + bitOffset / 8, val >> (bitOffset & ~7), 1);
        else
            writeEA(0, val);
    }
}

template <SReg sr>
void CPU::doLoadFarPointer()
{
    assert(currentInstruction.numOperands == 2);
    assert(currentInstruction.ea[0].type == DecodedEAType::reg16 || currentInstruction.ea[0].type == DecodedEAType::reg32);
    const auto farPtr = readFarPtr(currentInstruction.ea[1]);
    loadSreg(sr, farPtr.segment());
    writeEA(0, farPtr.offset());
}

void CPU::checkPriv(std::uint32_t errorCode)
{
    if (!protectedMode() || cpl() == 0)
        return;
    throw CPUException { CPUExceptionNumber::GeneralProtection, errorCode };
}

struct IMulResult {
    uint64_t product;
    bool overflow;
};

static IMulResult IMul(uint64_t l, uint64_t r, std::uint8_t size)
{
    auto product = static_cast<uint64_t>(static_cast<int64_t>(SignExtend(l, size)) * static_cast<int64_t>(SignExtend(r, size)));
    return { product, product != SignExtend(product, size) };
}

static void WriteDoubleReg(CPUState& state, std::uint64_t result, std::uint8_t halfSize)
{
    switch (halfSize) {
    case 1:
        UpdateU16(state.regs_[REG_AX], result);
        break;
    case 2:
        UpdateU16(state.regs_[REG_AX], result);
        UpdateU16(state.regs_[REG_DX], result >> 16);
        break;
    case 4:
        UpdateU32(state.regs_[REG_AX], result);
        UpdateU32(state.regs_[REG_DX], result >> 32);
        break;
    default:
        throw std::runtime_error { "Invalid size for GetDoubleReg: " + std::to_string(halfSize) };
    }
}

static std::uint64_t GetDoubleReg(const CPUState& state, std::uint8_t halfSize)
{
    switch (halfSize) {
    case 1:
        return GetU16(state.regs_[REG_AX]);
    case 2:
        return GetU16(state.regs_[REG_AX]) | static_cast<uint32_t>(GetU16(state.regs_[REG_DX])) << 16;
    case 4:
        return GetU32(state.regs_[REG_AX]) | static_cast<uint64_t>(GetU32(state.regs_[REG_DX])) << 32;
    default:
        throw std::runtime_error { "Invalid size for GetDoubleReg: " + std::to_string(halfSize) };
    }
}

#define BIN_BIT_OP(op)                          \
    do {                                        \
        l = readEA(0);                          \
        r = readEA(1);                          \
        result = l op r;                        \
        writeEA(0, result);                     \
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK; \
    } while (0)

void CPU::doStep()
{
    int64_t offset = 0;
    currentInstruction = Decode(cpuInfo(), [&]() { return readCodeByte(ip_ + (offset++), false); });
    const auto& ins = currentInstruction;

    ip_ += ins.numInstructionBytes;
    if (cpuModel_ < CPUModel::i80386)
        ip_ &= 0xffff;

    if ((ins.prefixes & PREFIX_LOCK) && cpuModel_ >= CPUModel::i80386) {
        //The LOCK prefix can be prepended only to the following instructions and only to those forms of the instructions where the destination operand is a memory operand:
        // ADD, ADC, AND, BTC, BTR, BTS, CMPXCHG, CMPXCH8B, CMPXCHG16B, DEC, INC, NEG, NOT, OR, SBB, SUB, XOR, XADD, and XCHG
        switch (ins.instruction->mnemonic) {
        case InstructionMnem::ADD:
        case InstructionMnem::ADC:
        case InstructionMnem::AND:
        case InstructionMnem::BTC:
        case InstructionMnem::BTR:
        case InstructionMnem::BTS:
        case InstructionMnem::DEC:
        case InstructionMnem::INC:
        case InstructionMnem::NEG:
        case InstructionMnem::NOT:
        case InstructionMnem::OR:
        case InstructionMnem::SBB:
        case InstructionMnem::SUB:
        case InstructionMnem::XOR:
            if (EAIsMemory(ins.ea[0].type))
                break;
            goto LockException;
        case InstructionMnem::XCHG:
            if (EAIsMemory(ins.ea[1].type))
                break;
            goto LockException;
        default:
LockException:
            THROW_UD("LOCK prefix used with {}", ins.mnemoic);
        }
    }

    for (auto& va : verifiedAddresses_)
        va.valid = false;

    uint32_t flagsMask = 0;
    uint64_t result = 0, carry = 0, l, r;
    //DEFAULT_EFLAGS_RESULT_MASK
    switch (ins.instruction->mnemonic) {
    case InstructionMnem::AAA:
        // TODO: OF/SF/ZF/PF
        if ((regs_[REG_AX] & 0xf) > 9 || (flags_ & EFLAGS_MASK_AF)) {
            if (cpuModel_ < CPUModel::i80386) {
                UpdateU8L(regs_[REG_AX], (regs_[REG_AX] + 6) & 0xf); // AL = (AL + 6) & 0xf
                UpdateU8H(regs_[REG_AX], (regs_[REG_AX] >> 8) + 1);  // AH += 1
            } else {
                UpdateU16(regs_[REG_AX], (regs_[REG_AX] + 0x106) & 0xff0f);
            }
            flags_ |= EFLAGS_MASK_CF | EFLAGS_MASK_AF;
        } else {
            flags_ &= ~(EFLAGS_MASK_CF | EFLAGS_MASK_AF);
            UpdateU8L(regs_[REG_AX], regs_[REG_AX] & 0xf);
        }
        break;
    case InstructionMnem::AAD:
        result = (GetU8L(regs_[REG_AX]) + GetU8H(regs_[REG_AX]) * readEA(0)) & 0xff;
        UpdateU16(regs_[REG_AX], result);
        flagsMask = EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_PF;
        currentInstruction.operationSize = 1;
        break;
    case InstructionMnem::AAM:
        // TODO: OF/AF/CF
        assert(currentInstruction.operationSize == 1);
        l = regs_[REG_AX] & 0xff;
        r = readEA(0) & 0xff;
        if (!r) {
            flags_ &= ~(EFLAGS_MASK_ZF | EFLAGS_MASK_SF | EFLAGS_MASK_AF); // TODO: flags on exception...
            //flags_ |= EFLAGS_MASK_PF;
            throw CPUException { CPUExceptionNumber::DivisionError };
        }
        result = l % r;
        UpdateU8L(regs_[REG_AX], result);
        UpdateU8H(regs_[REG_AX], l / r);
        flagsMask = EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_PF;
        break;
    case InstructionMnem::AAS:
        // TODO: OF/SF/ZF/PF
        if ((regs_[REG_AX] & 0xf) > 9 || (flags_ & EFLAGS_MASK_AF)) {
            if (cpuModel_ < CPUModel::i80386) {
                UpdateU8L(regs_[REG_AX], (regs_[REG_AX] - 6) & 0xf); // AL = (AL - 6) & 0xf
                UpdateU8H(regs_[REG_AX], (regs_[REG_AX] >> 8) - 1); // AH -= 1
            } else {
                auto ax = (regs_[REG_AX] & 0xffff) - 6;
                ax = ((ax - 0x100) & 0xff00) | (ax & 0x0f); // AH -= 1, AL &= 0xF^M
                UpdateU16(regs_[REG_AX], ax);
            }
            flags_ |= EFLAGS_MASK_CF | EFLAGS_MASK_AF;
        } else {
            flags_ &= ~(EFLAGS_MASK_CF | EFLAGS_MASK_AF);
            UpdateU8L(regs_[REG_AX], regs_[REG_AX] & 0xf);
        }
        break;

    case InstructionMnem::ADC:
        l = readEA(0);
        r = readEA(1);
        result = l + r + !!(flags_ & EFLAGS_MASK_CF);
        writeEA(0, result);
        HANDLE_ADD_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::ADD:
        l = readEA(0);
        r = readEA(1);
        result = l + r;
        writeEA(0, result);
        HANDLE_ADD_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::AND:
        BIN_BIT_OP(&);
        break;
    case InstructionMnem::ARPL:
        l = readEA(0);
        r = readEA(1);
        if ((l & DESC_MASK_DPL) < (r & DESC_MASK_DPL)) {
            flags_ |= EFLAGS_MASK_ZF;
            writeEA(0, (l & ~DESC_MASK_DPL) | (r & DESC_MASK_DPL));
        } else {
            flags_ &= ~EFLAGS_MASK_ZF;
        }
        break;
    case InstructionMnem::BOUND: {
        // Operand size attribute determines if it's 16/16 or 32/32
        if (!EAIsMemory(ins.ea[1].type))
            THROW_UD("Second operand for BOUND is not a memory location");

        l = SignExtend(readEA(0), ins.operandSize);
        auto addr = calcAddress(ins.ea[1]);
        int64_t lower = SignExtend(readMem(addr, ins.operandSize), ins.operandSize);
        addr.offset += ins.operandSize;
        addr.offset &= ins.addressMask();
        int64_t upper = SignExtend(readMem(addr, ins.operandSize), ins.operandSize);
        if (static_cast<int64_t>(l) < lower || static_cast<int64_t>(l) > upper) {
            if (exceptionTraceMask_ & (1 << CPUExceptionNumber::BoundRangeExceeded))
                std::println("{}Out of bounds: {} <= {} <= {}", IP_PREFIX(), lower, static_cast<int64_t>(l), upper);
            throw CPUException { CPUExceptionNumber::BoundRangeExceeded };
        }
        break;
    }
    case InstructionMnem::BSF:
        r = readEA(1);
        if (!r) {
            flags_ |= EFLAGS_MASK_ZF;
            // Dest is undefined
        } else {
            flags_ &= ~EFLAGS_MASK_ZF;
#ifdef _MSC_VER
            unsigned long index;
            _BitScanForward64(&index, r);
#else
            const auto index = __builtin_ctzl(r);
#endif
            writeEA(0, index);
        }
        break;
    case InstructionMnem::BSR:
        r = readEA(1);
        if (!r) {
            flags_ |= EFLAGS_MASK_ZF;
            // Dest is undefined
        } else {
            flags_ &= ~EFLAGS_MASK_ZF;
#ifdef _MSC_VER
            unsigned long index;
            _BitScanReverse64(&index, r);
#else
            const auto index = 8 * sizeof(r) - 1 - __builtin_clzl(r);
#endif
            writeEA(0, index);
        }
        break;
    case InstructionMnem::BT:
        doBitInstruction<InstructionMnem::BT>();
        break;
    case InstructionMnem::BTC:
        doBitInstruction<InstructionMnem::BTC>();
        break;
    case InstructionMnem::BTR:
        doBitInstruction<InstructionMnem::BTR>();
        break;
    case InstructionMnem::BTS:
        doBitInstruction<InstructionMnem::BTS>();
        break;
    case InstructionMnem::CALL:
        doNearControlTransfer(ControlTransferType::call);
        break;
    case InstructionMnem::CBW:
        UpdateU8H(regs_[REG_AX], regs_[REG_AX] & 0x80 ? 0xFF : 0x00);
        break;
    case InstructionMnem::CLTS:
        checkPriv(0);
        cregs_[0] &= ~(1 << 3); // Clear TS
        break;
    case InstructionMnem::CWD:
        regs_[REG_DX] = regs_[REG_AX] & 0x8000 ? 0xFFFF : 0x0000;
        break;
    case InstructionMnem::CWDE: {
        auto& ax = regs_[REG_AX];
        if (ins.operandSize == 2)
            UpdateU16(ax, SignExtend(ax, 1));
        else
            UpdateU32(ax, SignExtend(ax, 2));
        break;
    }
    case InstructionMnem::CDQ: {
        l = -static_cast<int64_t>((regs_[REG_AX] >> (8 * ins.operandSize - 1)) & 1);
        Update(regs_[REG_DX], l, ins.operandSize);
        break;
    }
    case InstructionMnem::CLC:
        flags_ &= ~EFLAGS_MASK_CF;
        break;
    case InstructionMnem::CLD:
        flags_ &= ~EFLAGS_MASK_DF;
        break;
    case InstructionMnem::CLI:
        checkPriv(0);
        flags_ &= ~EFLAGS_MASK_IF;
        break;
    case InstructionMnem::CMC:
        flags_ ^= EFLAGS_MASK_CF;
        break;
    case InstructionMnem::CMP:
        l = readEA(0);
        r = readEA(1);
        result = l - r;
        HANDLE_SUB_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::CMPS:
    case InstructionMnem::CMPSB:
        doStringInstruction<InstructionMnem::CMPS>();
        break;
    case InstructionMnem::DAA:
    case InstructionMnem::DAS:
    {
        // https://www.righto.com/2023/01/understanding-x86s-decimal-adjust-after.html
        assert(currentInstruction.operationSize == 1);
        const int32_t adjust = ins.instruction->mnemonic == InstructionMnem::DAA ? 6 : -6;
        const auto old_AL = static_cast<uint8_t>(regs_[REG_AX]);
        const uint8_t upperCheck = (cpuModel_ <= CPUModel::i8086 && (flags_ & EFLAGS_MASK_AF)) ? 0x9F : 0x99;
        const bool old_CF = (flags_ & EFLAGS_MASK_CF);
        if ((old_AL & 0xf) > 9 || (flags_ & EFLAGS_MASK_AF)) {
            AddReg(regs_[REG_AX], adjust, 1);
            flags_ |= EFLAGS_MASK_AF;
            if (cpuModel_ >= CPUModel::i80386 && adjust < 0 && (old_AL - 6) < 0)
                flags_ |= EFLAGS_MASK_CF;
        }
        if (old_AL > upperCheck || old_CF) {
            AddReg(regs_[REG_AX], adjust << 4, 1);
            flags_ |= EFLAGS_MASK_CF;
        }
        // OF is undefined, but set only if bit 7 changes from 0 to 1
        // On 8088 this it's the opposite for DAS
        if (cpuModel_ <= CPUModel::i8086 && ins.instruction->mnemonic == InstructionMnem::DAS) {
            SetFlag(flags_, EFLAGS_MASK_OF, (old_AL & 0x80) && !(regs_[REG_AX] & 0x80));
        } else {
            SetFlag(flags_, EFLAGS_MASK_OF, !(old_AL & 0x80) && (regs_[REG_AX] & 0x80));
        }
        result = regs_[REG_AX] & 0xff;
        flagsMask = EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_PF;
        break;
    }
    case InstructionMnem::DEC:
        l = readEA(0);
        r = 1;
        result = l - r;
        writeEA(0, result);
        HANDLE_SUB_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~EFLAGS_MASK_CF; // Carry not updated
        break;
    case InstructionMnem::ENTER: {
        auto allocSize = static_cast<uint16_t>(readEA(0) & 0xffff);
        auto nestingLevel = readEA(1) & 31;
        const auto oldSP = regs_[REG_SP];
        const auto oldBP = regs_[REG_BP];

        if (nestingLevel > 1 && ((regs_[REG_BP] - ins.operandSize) & stackMask()) + ins.operandSize - 1 > sdesc_[SREG_SS].limit) {
            if (exceptionTraceMask_ & (1 << CPUExceptionNumber::StackSegmentFault))
                std::println("{}(E)BP would be outside stack limit", IP_PREFIX());
            throw CPUException { CPUExceptionNumber::StackSegmentFault };
        }
        try {
            push(regs_[REG_BP], ins.operandSize);
            auto frameTemp = Get(regs_[REG_SP], ins.operandSize);
            for (; nestingLevel > 1; nestingLevel--) {
                AddReg(regs_[REG_BP], -ins.operandSize, stackSize());
                push(readMem(SegmentedAddress { SREG_SS, regs_[REG_BP] & stackMask() }, ins.operandSize), ins.operandSize);
            }
            if (nestingLevel)
                push(frameTemp, ins.operandSize);
            Update(regs_[REG_BP], frameTemp, ins.operandSize);
            AddReg(regs_[REG_SP], -allocSize, stackSize());
            // PF if a write using the final value of the stack pointer (within the current stack segment) would cause a page fault
            if (pagingEnabled()) {
                pageLookup(toLinearAddress(currentSp(), 1), PL_MASK_W);
            }
        } catch (const CPUException&) {
            regs_[REG_BP] = oldBP;
            regs_[REG_SP] = oldSP;
            throw;
        }
        break;
    }
    case InstructionMnem::ESC:
    case InstructionMnem::FWAIT:
        //std::print("Warning: Ignoring ESC {:02X}{:02X}\n", ins.instructionBytes[0], ins.instructionBytes[1]);
        break;
    case InstructionMnem::IN:
        l = readEA(1);
        if (ins.ea[1].type == DecodedEAType::imm8)
            l &= 0xff;
        writeEA(0, bus_.ioInput(static_cast<uint16_t>(l), ins.opcode == 0xE4 || ins.opcode == 0xEC ? 1 : ins.operandSize));
        break;
    case InstructionMnem::INS:
    case InstructionMnem::INSB:
        doStringInstruction<InstructionMnem::INS>();
        break;
    case InstructionMnem::INC:
        l = readEA(0);
        r = 1;
        result = l + r;
        writeEA(0, result);
        HANDLE_ADD_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~EFLAGS_MASK_CF; // Carry not updated
        break;
    case InstructionMnem::INT:
        doInterrupt(static_cast<uint8_t>(ins.ea[0].immediate), false);
        break;
    case InstructionMnem::INT3:
        doInterrupt(3, false);
        break;
    case InstructionMnem::INTO:
        if (flags_ & EFLAGS_MASK_OF)
            doInterrupt(CPUExceptionNumber::Overflow, false);
        break;
    case InstructionMnem::LEAVE: {
        const auto oldBp = readMem(SegmentedAddress { SREG_SS, regs_[REG_BP] & stackMask() }, ins.operandSize);
        Update(regs_[REG_SP], regs_[REG_BP], stackSize());
        Update(regs_[REG_BP], oldBp, ins.operandSize);
        updateSp(1);
        break;
    }
    case InstructionMnem::IMUL: {
        IMulResult res;
        if (ins.numOperands == 1) {
            r = readEA(0);
            res = IMul(regs_[REG_AX], r, ins.operandSize);
            WriteDoubleReg(*this, res.product, ins.operandSize);
        } else if (ins.numOperands == 2) {
            res = IMul(readEA(0), readEA(1), ins.operandSize);
            writeEA(0, res.product);
        } else if (ins.numOperands == 3) {
            res = IMul(readEA(1), readEA(2), ins.operandSize);
            writeEA(0, res.product);
        } else {
            throw std::runtime_error { "TODO: IMUL with " + std::to_string(ins.numOperands) + " operands" };
        }
        flags_ &= ~(EFLAGS_MASK_CF | EFLAGS_MASK_OF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_AF | EFLAGS_MASK_PF);
        if (res.overflow)
            flags_ |= EFLAGS_MASK_CF | EFLAGS_MASK_OF;

        // 8088/8086 flags (except CF and OF) are set by the ALU operation in "IMULCOF"
        // ADC of tmpA and ZERO with CF from LRCY of tmpC
        if (/*ins.numOperands == 1*/1) {
            const auto halfShift = 8 * ins.operandSize;
            const auto tmpA = res.product >> halfShift;
            const auto tmpC_msb = (res.product >> (halfShift - 1)) & 1;
            l = tmpA;
            r = tmpC_msb;
            result = l + r;
            HANDLE_ADD_CARRY();
            flagsMask = EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_AF | EFLAGS_MASK_PF;
        }
        break;
    }
    case InstructionMnem::MUL:
        if (ins.numOperands != 1)
            throw std::runtime_error { "TODO: MUL with multiple operands" };
        result = Get(regs_[REG_AX], ins.operandSize) * readEA(0);
        WriteDoubleReg(*this, result, ins.operandSize);

        //
        // On 8088/8086 flags are set according to "tmpA" by passing it (unmodified) through the ALU
        // https://www.righto.com/2023/03/8086-multiplication-microcode.html
        //
        flags_ &= ~(EFLAGS_MASK_CF | EFLAGS_MASK_OF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_AF | EFLAGS_MASK_PF);
        if (result >> 8 * ins.operandSize)
            flags_ |= EFLAGS_MASK_CF | EFLAGS_MASK_OF;
        else
            flags_ |= EFLAGS_MASK_ZF;
        if (result >> (16*ins.operandSize-1))
            flags_ |= EFLAGS_MASK_SF;

        if (cpuModel_ <= CPUModel::i8086) {
            if (Parity(static_cast<uint8_t>(ins.operandSize == 1 ? regs_[REG_AX] >> 8 : regs_[REG_DX])))
                flags_ |= EFLAGS_MASK_PF;
        }

        break;
    case InstructionMnem::IDIV: {
        r = SignExtend(readEA(0), ins.operandSize);
        if (!r)
            throw CPUException { CPUExceptionNumber::DivisionError };
        l = SignExtend(GetDoubleReg(*this, ins.operandSize), ins.operandSize * 2);
        int64_t q = static_cast<int64_t>(l) / static_cast<int64_t>(r);
        int64_t rem = static_cast<int64_t>(l) % static_cast<int64_t>(r);

        if (cpuModel_ <= CPUModel::i8086 && (ins.prefixes & PREFIX_REP_MASK)) {
            // https://www.reenigne.org/blog/8086-microcode-disassembled/
            // "Using the REP or REPNE prefix with an IDIV instruction negates the quotient"
            q = -q;
        }

        // N.B. 8088/8006 does not allow INTx_MIN!
        if (ins.operandSize == 1) {
            if (cpuModel_ == CPUModel::i80386 && q < INT8_MIN) {
                // Very weird behavior seen in 386 SingleStepTests. Obviously this isn't what actually happens in the CPU, but it matches.
                q = static_cast<int64_t>(l ^ 0x4000) / static_cast<int64_t>(r);
                rem = static_cast<int64_t>(l ^ 0x4000) % static_cast<int64_t>(r);
                if (q != INT8_MIN)
                    throw CPUException { CPUExceptionNumber::DivisionError };
            }
            if (q < INT8_MIN || q > INT8_MAX || (cpuModel_ <= CPUModel::i8086 && q == INT8_MIN))
                throw CPUException { CPUExceptionNumber::DivisionError };
            UpdateU8L(regs_[REG_AX], q);
            UpdateU8H(regs_[REG_AX], rem);
        } else if (ins.operandSize == 2) {
            if (q < INT16_MIN || q > INT16_MAX || (cpuModel_ <= CPUModel::i8086 && q == INT16_MIN))
                throw CPUException { CPUExceptionNumber::DivisionError };
            UpdateU16(regs_[REG_AX], q);
            UpdateU16(regs_[REG_DX], rem);
        } else if (ins.operandSize == 4) {
            if (q < INT32_MIN || q > INT32_MAX)
                throw CPUException { CPUExceptionNumber::DivisionError };
            UpdateU32(regs_[REG_AX], q);
            UpdateU32(regs_[REG_DX], rem);
        } else {
            throw std::runtime_error { "TODO: IDIV with larger operand size" };
        }
        break;
    }
    case InstructionMnem::DIV:
        r = readEA(0);
        if (!r)
            throw CPUException { CPUExceptionNumber::DivisionError };
        l = GetDoubleReg(*this, ins.operandSize);
        result = l / r;
        if (result >> 8 * ins.operandSize) {
            throw CPUException { CPUExceptionNumber::DivisionError };
        }
        if (ins.operandSize == 1) {
            UpdateU8L(regs_[REG_AX], result);
            UpdateU8H(regs_[REG_AX], l % r);
        } else {
            Update(regs_[REG_AX], result, ins.operandSize);
            Update(regs_[REG_DX], l % r, ins.operandSize);
        }
        break;
    case InstructionMnem::JCXZ:
        // N.B. the size is determined by the address size..
        if ((regs_[REG_CX] & ins.addressMask()) == 0)
            doNearControlTransfer(ControlTransferType::jump);
        break;
    case InstructionMnem::HLT:
        checkPriv(0);
        if (!(flags_ & EFLAGS_MASK_IF))
            throw CPUHaltedException { };
        halted_ = true;
        break;
    case InstructionMnem::CALLF:
    case InstructionMnem::JMPF: {
        uint16_t cs;
        uint64_t ip;
        if (ins.ea[0].type == DecodedEAType::abs16_16) {
            cs = static_cast<uint16_t>(ins.ea[0].address >> 16);
            ip = ins.ea[0].address & 0xffff;
        } else if (ins.ea[0].type == DecodedEAType::abs16_32) {
            cs = static_cast<uint16_t>(ins.ea[0].address >> 32);
            ip = ins.ea[0].address & 0xffffffff;
        } else {
            const auto farPtr = readFarPtr(ins.ea[0]);
            cs = farPtr.segment();
            ip = farPtr.offset();
        }
        doControlTransfer(cs, ip, ins.mnemoic == InstructionMnem::CALLF ? ControlTransferType::call : ControlTransferType::jump);
        break;
    }
    case InstructionMnem::JO: // 70
    case InstructionMnem::JNO: // 71
    case InstructionMnem::JB: // 72
    case InstructionMnem::JNB: // 73
    case InstructionMnem::JZ: // 74
    case InstructionMnem::JNZ: // 75
    case InstructionMnem::JBE: // 76
    case InstructionMnem::JNBE: // 77
    case InstructionMnem::JS: // 78
    case InstructionMnem::JNS: // 79
    case InstructionMnem::JP: // 7A
    case InstructionMnem::JNP: // 7B
    case InstructionMnem::JL: // 7C
    case InstructionMnem::JNL: // 7D
    case InstructionMnem::JLE: // 7E
    case InstructionMnem::JNLE: // 7E
        if (EvalCond(flags_, ins.opcode & 0xf))
            doNearControlTransfer(ControlTransferType::jump);
        break;
    case InstructionMnem::JMP:
        doNearControlTransfer(ControlTransferType::jump);
        break;
    case InstructionMnem::LAHF:
        UpdateU8H(regs_[REG_AX], flags_);
        break;
    case InstructionMnem::LEA:
        if (cpuModel_ >= CPUModel::i8086 && !EAIsMemory(ins.ea[1].type))
            THROW_UD("LEA with non-memory {}", ins.ea[1]);
        writeEA(0, calcAddress(ins.ea[1]).offset);
        break;
    case InstructionMnem::LGDT:
    case InstructionMnem::LIDT: {
        assert(ins.operandSize == 2 || ins.operandSize == 4);
        auto addr = calcAddress(ins.ea[0]);
        auto& table = ins.mnemoic == InstructionMnem::LGDT ? gdt_ : idt_;
        const auto limit = static_cast<uint16_t>(readMem(addr, 2));
        addr.offset += 2;
        auto base = readMem(addr, 4);
        if (ins.operandSize == 2)
            base &= 0xffffff;
        table.limit = limit;
        table.base = base;
        std::print("{} limit=0x{:04X} base=0x{:08X}\n", ins.mnemoic, limit, base);
        break;
    }
    case InstructionMnem::LLDT: {
        const auto index = static_cast<uint16_t>(readEA(0));
        assert(!(index & 7));
        const auto desc = readDescriptor(index);
        if ((desc.access & (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S | SD_ACCESS_MASK_TYPE)) != (SD_ACCESS_MASK_P | SD_TYPE_LDT))
            throw std::runtime_error { std::format("Invalid LDT descriptor {}", desc) };
        ldt_ = desc;
        break;
    }
    case InstructionMnem::LTR: {
        const auto index = static_cast<uint16_t>(readEA(0));
        assert(!(index & 7));
        const auto desc = readDescriptor(index);
        if ((desc.access & (SD_ACCESS_MASK_P | SD_ACCESS_MASK_S | SD_ACCESS_MASK_TYPE)) != (SD_ACCESS_MASK_P | SD_TYPE_TASK32_AVAILABLE))
            throw std::runtime_error { std::format("Invalid TASK descriptor {}", desc) };
        task_ = desc;
        break;
    }
    case InstructionMnem::LDS:
        doLoadFarPointer<SREG_DS>();
        break;
    case InstructionMnem::LES:
        doLoadFarPointer<SREG_ES>();
        break;
    case InstructionMnem::LFS:
        doLoadFarPointer<SREG_FS>();
        break;
    case InstructionMnem::LGS:
        doLoadFarPointer<SREG_GS>();
        break;
    case InstructionMnem::LSS:
        doLoadFarPointer<SREG_SS>();
        break;
    case InstructionMnem::LODS:
    case InstructionMnem::LODSB:
        doStringInstruction<InstructionMnem::LODS>();
        break;
    case InstructionMnem::LOOP:
        l = 1;
        goto DoLoop;
    case InstructionMnem::LOOPZ:
        l = !!(flags_ & EFLAGS_MASK_ZF);
        goto DoLoop;
    case InstructionMnem::LOOPNZ:
        l = !(flags_ & EFLAGS_MASK_ZF);
DoLoop:
        assert(ins.addressSize == 2 || ins.addressSize == 4);
        result = AddReg(regs_[REG_CX], -1, ins.addressSize);
        if (result != 0 && l)
            doNearControlTransfer(ControlTransferType::jump);
        break;
    case InstructionMnem::MOV:
        if (cpuModel_ > CPUModel::i8086 && ins.ea[0].type == DecodedEAType::sreg) {
            checkSreg(ins.ea[0].regNum); // Need check before potential read from memory
            if (ins.ea[0].regNum == SREG_CS)
                THROW_UD("MOV to CS");
        }
        writeEA(0, readEA(1));
        break;
    case InstructionMnem::MOVS:
    case InstructionMnem::MOVSB:
        doStringInstruction<InstructionMnem::MOVS>();
        break;
    case InstructionMnem::MOVSX:
        writeEA(0, SignExtend(readEA(1), ins.operandSize));
        break;
    case InstructionMnem::MOVZX:
        writeEA(0, readEA(1));
        break;
    case InstructionMnem::NEG:
        l = 0;
        r = readEA(0);
        result = l - r;
        writeEA(0, result);
        HANDLE_SUB_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::NOP:
        break;
    case InstructionMnem::NOT:
        // NB doesn't update flags
        writeEA(0, ~readEA(0));
        break;
    case InstructionMnem::OR:
        BIN_BIT_OP(|);
        break;
    case InstructionMnem::OUT:
        l = readEA(0);
        r = readEA(1);
        if (ins.ea[0].type == DecodedEAType::imm8)
            l &= 0xff;
        bus_.ioOutput(static_cast<uint16_t>(l), static_cast<uint32_t>(r), ins.opcode == 0xE6 || ins.opcode == 0xEE ? 1 : ins.operandSize);
        break;
    case InstructionMnem::OUTS:
    case InstructionMnem::OUTSB:
        doStringInstruction<InstructionMnem::OUTS>();
        break;
    case InstructionMnem::POP:
        if (ins.ea[0].type == DecodedEAType::sreg) {
            // Like PUSH it appears that only a word is read for a SREG
            const auto res = readMem(currentSp(), 2);
            updateSp(1);
            writeEA(0, res);
        } else if (cpuModel_ >= CPUModel::i80286 && EAIsMemory(ins.ea[0].type)) {
            const auto oldSp = regs_[REG_SP];
            try {
                const auto spAddr = verifyAddress(currentSp(), ins.operandSize, true);
                updateSp(1); // Increment SP before EA calculation
                const auto addr = verifyAddress(calcAddress(ins.ea[0]), ins.operandSize, true);
                writeMemPhysical(addr, readMemPhysical(spAddr, ins.operandSize), ins.operandSize);
            } catch (...) {
                regs_[REG_SP] = oldSp;
                throw;
            }
        } else {
            writeEA(0, pop(ins.operandSize));
        }
        break;
    case InstructionMnem::PUSH:
        if (cpuModel_ <= CPUModel::i8086 && ins.ea[0].type == DecodedEAType::reg16 && ins.ea[0].regNum == REG_SP) { // PUSH SP, the value pushed has already been updated
            assert(ins.operandSize == 2);
            push((regs_[REG_SP] - 2) & 0xffff, ins.operandSize);
        } else if (ins.ea[0].type == DecodedEAType::sreg) {
            // If the source operand is a segment register (16 bits) [...] the segment selector is written on the stack using a 16-bit move
            updateSp(-1);            
            writeMem(currentSp(), readEA(0), 2);
        } else {
            push(readEA(0), ins.operandSize);
        }
        break;
    case InstructionMnem::POPA: {
        // Undocumented behavior, (E)SP is actually popped, but usually overwritten at the end
        auto tempSp = currentSp();
        for (int reg = REG_DI; reg >= REG_AX; reg--) {
            const auto val = readMem(tempSp, ins.operandSize);
            if (reg != REG_SP || cpuModel_ < CPUModel::i80586) // TOOD where does this change
                Update(regs_[reg], val, ins.operandSize);
            tempSp.offset += ins.operandSize;
            tempSp.offset &= stackMask();
        }
        Update(regs_[REG_SP], tempSp.offset, stackSize());
        break;
    }
    case InstructionMnem::POPF:
        assert(ins.operandSize == 2 || ins.operandSize == 4);
        if (ins.operandSize == 2)
            setFlags((flags_ & 0xffff0000) | static_cast<uint32_t>(pop(2)));
        else
            setFlags(static_cast<uint32_t>(pop(4)));
        break;
    case InstructionMnem::PUSHA:
        for (int reg = REG_DI; reg >= REG_AX; reg--)
            writeStack(reg, regs_[reg]);
        updateSp(-8);
        break;
    case InstructionMnem::PUSHF:
        // ?? At least for i386 it seems like the upper bits read as zero
        push(flags_ & 0xffff, ins.operandSize);
        break;
    case InstructionMnem::IRET:
        doInterruptReturn();
        break;
    case InstructionMnem::RETF: {
        const auto ip = readStack(0);
        const auto cs = static_cast<uint16_t>(readStack(1));
        if (cpuModel_ >= CPUModel::i80286 && ip > sdesc_[SREG_CS].limit)
            THROW_GP("RETN -  return instruction pointer is not within the return code segment limit");
        updateSp(2);
        doFarJump(cs, ip);
        if (currentInstruction.numOperands)
            AddReg(regs_[REG_SP], static_cast<uint32_t>(readEA(0)), stackSize());
        break;
    }
    case InstructionMnem::RETN: {
        auto retAddress = readStack(0);
        if (cpuModel_ >= CPUModel::i80286 && retAddress > sdesc_[SREG_CS].limit)
            THROW_GP("RETN -  return instruction pointer is not within the return code segment limit");
        updateSp(1);
        if (currentInstruction.numOperands) {
            auto tempSp = regs_[REG_SP];
            AddReg(tempSp, static_cast<uint32_t>(readEA(0)), stackSize());
            if (cpuModel_ >= CPUModel::i80286 && tempSp > sdesc_[SREG_SS].limit)
                THROW_GP("RETN -  statkc pointer is not within limit");
            regs_[REG_SP] = tempSp;
        }
        Update(ip_, retAddress, currentInstruction.operandSize);
        break;
    }
    case InstructionMnem::SALC:
        UpdateU8L(regs_[REG_AX], flags_ & EFLAGS_MASK_CF ? 0xFF : 0x00);
        break;
    case InstructionMnem::SAHF:
        setFlags((flags_ & ~0xff) | GetU8H(regs_[REG_AX]));
        break;
    case InstructionMnem::SETB:
    case InstructionMnem::SETBE:
    case InstructionMnem::SETL:
    case InstructionMnem::SETLE:
    case InstructionMnem::SETNB:
    case InstructionMnem::SETNBE:
    case InstructionMnem::SETNL:
    case InstructionMnem::SETNLE:
    case InstructionMnem::SETNO:
    case InstructionMnem::SETNP:
    case InstructionMnem::SETNS:
    case InstructionMnem::SETNZ:
    case InstructionMnem::SETO:
    case InstructionMnem::SETP:
    case InstructionMnem::SETS:
    case InstructionMnem::SETZ:
        writeEA(0, EvalCond(flags_, ins.opcode & 0xf));
        break;
    case InstructionMnem::SETMO:
        if (readEA(1)) {
            result = UINT64_MAX;
            writeEA(0, result);
            flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        }
        break;
    case InstructionMnem::SAL:
    case InstructionMnem::SHL:
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        if (r) {
            result = l << r;
            carry = l << (r - 1);
            writeEA(0, result);
            flags_ &= ~EFLAGS_MASK_OF;
            if (((result ^ carry) >> (8 * ins.operationSize - 1)) & 1)
                flags_ |= EFLAGS_MASK_OF;
            flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~EFLAGS_MASK_OF;

            // CF is undefined if count > size
            if (ins.operationSize == 1 && cpuModel_ == CPUModel::i80386 && r > 8) {
                if ((r == 16 || r == 24) && (l & 1))
                    flags_ |= EFLAGS_MASK_CF | EFLAGS_MASK_OF;
                else
                    flags_ &= ~(EFLAGS_MASK_CF | EFLAGS_MASK_OF);
                flagsMask &= ~EFLAGS_MASK_CF;
            }
        }
        break;
    case InstructionMnem::SHLD: {
        const auto msbShift = ins.operandSize * 8 - 1;
        // result is undefined if count > size
        const int shift = readEA(2) & 31;
        result = readEA(0);
        r = readEA(1);
        if (!shift)
            break;
        auto cy = 0;
        for (int i = 0; i < shift; ++i) {
            cy = (r >> msbShift) & 1;
            r = r << 1 | r >> msbShift;
            carry = result;
            result = result << 1 | cy;
        }
        writeEA(0, result);
        SetFlag(flags_, EFLAGS_MASK_OF, (((result ^ carry) >> msbShift) & 1));
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~(EFLAGS_MASK_OF | EFLAGS_MASK_AF);
        break;
    }
    case InstructionMnem::SAR:
        l = SignExtend(readEA(0), ins.operandSize);
        r = readEA(1) & shiftMask_;
        if (r) {
            result = static_cast<int64_t>(l) >> r;
            carry = static_cast<int64_t>(l) >> (r - 1);
            writeEA(0, result);
            flags_ &= ~(EFLAGS_MASK_OF | EFLAGS_MASK_CF | EFLAGS_MASK_AF);
            if (carry & 1)
                flags_ |= EFLAGS_MASK_CF;
            flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~(EFLAGS_MASK_OF | EFLAGS_MASK_CF | EFLAGS_MASK_AF);
        }
        break;
    case InstructionMnem::SHR:
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        if (r) {
            result = l >> r;
            carry = l >> (r - 1);
            flags_ &= ~(EFLAGS_MASK_OF | EFLAGS_MASK_CF | EFLAGS_MASK_AF);
            if (carry & 1)
                flags_ |= EFLAGS_MASK_CF;
            // (1-bit shift only...) For the SHR instruction, the OF flag is set to the most-significant bit of the original operand.
            if (r == 1 && l >> (8 * ins.operandSize - 1))
                flags_ |= EFLAGS_MASK_OF;
            // Update flags before writing back result
            updateFlags(result, carry, DEFAULT_EFLAGS_RESULT_MASK & ~(EFLAGS_MASK_OF | EFLAGS_MASK_CF | EFLAGS_MASK_AF));
            // result is undefined if count > size
            if (ins.operationSize == 1 && cpuModel_ == CPUModel::i80386 && r > 8) {
                flags_ &= ~EFLAGS_MASK_OF; // Always cleared
                if ((r == 16 || r == 24) && (l & 0x80))
                    flags_ |= EFLAGS_MASK_CF;
                else
                    flags_ &= ~EFLAGS_MASK_CF;
            }
            writeEA(0, result);
        }
        break;
    case InstructionMnem::SHRD: {
        const auto msbShift = ins.operandSize * 8 - 1;
        // result is undefined if count > size
        const int shift = readEA(2) & 31;
        result = readEA(0);
        r = readEA(1);
        if (!shift)
            break;
        auto cy = 0;
        bool overflow = false;
        for (int i = 0; i < shift; ++i) {
            cy = r & 1;
            r = r << msbShift | r >> 1;
            carry = result << msbShift;
            overflow = cy ^ ((result >> msbShift) & 1);
            result = result >> 1 | static_cast<uint64_t>(cy) << msbShift;
        }
        writeEA(0, result);
        if (overflow)
            flags_ |= EFLAGS_MASK_OF;
        else
            flags_ &= ~EFLAGS_MASK_OF;
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK & ~EFLAGS_MASK_OF;
        break;
    }
    case InstructionMnem::RCL: {
        const auto width = ins.operationSize * 8;
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        uint64_t overflow = 0;
        carry = (flags_ & EFLAGS_MASK_CF) != 0;
        for (int i = 0; i < static_cast<int>(r); ++i) {
            const auto oldCy = carry;
            carry = (l >> (width - 1)) & 1;
            l <<= 1;
            l |= oldCy;
            overflow = (carry ^ (l >> (width - 1))) & 1;
        }
        SetFlag(flags_, EFLAGS_MASK_CF, carry != 0);
        if (r)
            SetFlag(flags_, EFLAGS_MASK_OF, overflow != 0);
        writeEA(0, l);
        break;
    }
    case InstructionMnem::RCR: {
        const auto width = ins.operationSize * 8;
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        uint64_t overflow = 0;
        carry = (flags_ & EFLAGS_MASK_CF) != 0;
        for (int i = 0; i < static_cast<int>(r); ++i) {
            const auto oldCy = carry;
            carry = l & 1;
            l >>= 1;
            overflow = (oldCy ^ (l >> (width - 2))) & 1;
            l |= oldCy << (width - 1);
        }
        SetFlag(flags_, EFLAGS_MASK_CF, carry != 0);
        if (r)
            SetFlag(flags_, EFLAGS_MASK_OF, overflow != 0);
        writeEA(0, l);
        break;
    }
    case InstructionMnem::ROL: {
        const auto width = ins.operationSize * 8;
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        uint64_t overflow = 0;
        carry = flags_ & EFLAGS_MASK_CF;
        for (int i = 0; i < static_cast<int>(r); ++i) {
            carry = (l >> (width - 1)) & 1;
            l <<= 1;
            overflow = (carry ^ (l >> (width - 1))) & 1;
            l |= carry;
        }
        SetFlag(flags_, EFLAGS_MASK_CF, carry != 0);
        if (r)
            SetFlag(flags_, EFLAGS_MASK_OF, overflow != 0);
        writeEA(0, l);
        break;
    }
    case InstructionMnem::ROR: {
        const auto width = ins.operationSize * 8;
        l = readEA(0);
        r = readEA(1) & shiftMask_;
        uint64_t overflow = 0;
        carry = flags_ & EFLAGS_MASK_CF;
        for (int i = 0; i < static_cast<int>(r); ++i) {
            carry = l & 1;
            l >>= 1;
            overflow = (carry ^ (l >> (width - 2))) & 1;
            l |= carry << (width - 1);
        }
        SetFlag(flags_, EFLAGS_MASK_CF, carry != 0);
        if (r)
            SetFlag(flags_, EFLAGS_MASK_OF, overflow != 0);
        writeEA(0, l);
        break;
    }
    case InstructionMnem::SBB:
        l = readEA(0);
        r = readEA(1);
        result = l - r - !!(flags_ & EFLAGS_MASK_CF);
        writeEA(0, result);
        HANDLE_SUB_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::SUB:
        l = readEA(0);
        r = readEA(1);
        result = l - r;
        writeEA(0, result);
        HANDLE_SUB_CARRY();
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::SCAS:
    case InstructionMnem::SCASB:
        doStringInstruction<InstructionMnem::SCAS>();
        break;
    case InstructionMnem::STC:
        flags_ |= EFLAGS_MASK_CF;
        break;
    case InstructionMnem::STD:
        flags_ |= EFLAGS_MASK_DF;
        break;
    case InstructionMnem::STI:
        flags_ |= EFLAGS_MASK_IF;
        break;
    case InstructionMnem::STOS:
    case InstructionMnem::STOSB:
        doStringInstruction<InstructionMnem::STOS>();
        break;
    case InstructionMnem::TEST:
        l = readEA(0);
        r = readEA(1);
        result = l & r;
        flagsMask = DEFAULT_EFLAGS_RESULT_MASK;
        break;
    case InstructionMnem::VERR:
    case InstructionMnem::VERW: {
        const auto seg = static_cast<uint16_t>(readEA(0));
        flags_ &= ~EFLAGS_MASK_ZF;
        if (!seg)
            break;
        try {
            const auto desc = readDescriptor(seg);
            if (!(desc.access & SD_ACCESS_MASK_S))
                break;
            if ((desc.access & (SD_ACCESS_MASK_E | SD_ACCESS_MASK_DC)) != (SD_ACCESS_MASK_E | SD_ACCESS_MASK_DC) &&
                (cpl() > desc.dpl() || (seg & DESC_MASK_DPL) > desc.dpl()))
                break;
            bool accessOk = false;
            switch (desc.access & (SD_ACCESS_MASK_E | SD_ACCESS_MASK_RW)) {
            case 0: // Readable data segment
            case SD_ACCESS_MASK_E | SD_ACCESS_MASK_RW: // Readable code segment
                accessOk = ins.mnemoic == InstructionMnem::VERR;
                break;
            case SD_ACCESS_MASK_RW: // Writeable data segment
                accessOk = true;
                break;
            }
            if (accessOk)
                flags_ |= EFLAGS_MASK_ZF;
        } catch (const CPUException& e) {
            assert(e.exceptionNo() == CPUExceptionNumber::GeneralProtection && e.errorCode() == static_cast<uint32_t>(seg & ~DESC_MASK_DPL));
            (void)e;
        }
        break;
    }
    case InstructionMnem::XCHG:
        l = readEA(0);
        r = readEA(1);
        // Write to mem first (in case the register is used for the EA)
        writeEA(1, l);
        writeEA(0, r);
        break;
    case InstructionMnem::XLAT: {
        // PCXT bios uses a segment override for XLAT even though that isn't documented as working for <386 processors..
        const auto sr = (currentInstruction.prefixes & PREFIX_SEG_MASK) ? static_cast<SReg>(((currentInstruction.prefixes & PREFIX_SEG_MASK) >> PREFIX_SEG_SHIFT) - 1) : SREG_DS;
        UpdateU8L(regs_[REG_AX], readMem(SegmentedAddress { sr, (regs_[REG_BX] + (regs_[REG_AX] & 0xff)) & ins.addressMask() }, 1));
        break;
    }
    case InstructionMnem::XOR:
        BIN_BIT_OP(^);
        break;

    case InstructionMnem::UNDEF:
        THROW_UD("Undefined instruction {}", HexString(ins.instructionBytes, ins.numInstructionBytes));

    default:
        throw std::runtime_error { std::string(MnemonicText(ins.instruction->mnemonic)) + " is not yet implemented" };
    }

    if (flagsMask)
        updateFlags(result, carry, flagsMask);
}
