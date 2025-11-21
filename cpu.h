#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <functional>
#include "cpu_descriptor.h"
#include "cpu_registers.h"
#include "decode.h"

constexpr std::uint32_t CR0_BIT_PE = 0; // Protected Mode Enable
constexpr std::uint32_t CR0_BIT_WP = 16; // Write protect (CPU cannot write to R/O pages in ring 0)
constexpr std::uint32_t CR0_BIT_PG = 31; // Paging

constexpr std::uint32_t CR0_MASK_PE = 1U << CR0_BIT_PE; // Protected Mode Enable
constexpr std::uint32_t CR0_MASK_WP = 1U << CR0_BIT_WP; // Write protect
constexpr std::uint32_t CR0_MASK_PG = 1U << CR0_BIT_PG; // Paging

struct CPUState {
    std::uint64_t regs_[16];
    std::uint16_t sregs_[6];
    std::uint64_t cregs_[8];
    std::uint64_t ip_;
    std::uint32_t flags_;
    DescriptorTable idt_;
    DescriptorTable gdt_;
    SegmentDescriptor sdesc_[6];
    SegmentDescriptor ldt_;
    SegmentDescriptor task_;

    bool protectedMode() const
    {
        return (cregs_[0] & CR0_MASK_PE) != 0;
    }

    bool pagingEnabled() const
    {
        return (cregs_[0] & (CR0_MASK_PE | CR0_MASK_PG)) == (CR0_MASK_PE | CR0_MASK_PG);
    }

    std::uint8_t cpl() const
    {
        return sregs_[SREG_CS] & DESC_MASK_DPL;
    }

    std::uint8_t defaultOperandSize() const
    {
        return protectedMode() && (sdesc_[SREG_CS].flags & SD_FLAGS_MASK_DB) ? 4 : 2;
    }

    std::uint8_t stackSize() const
    {
        return sdesc_[SREG_SS].flags & SD_FLAGS_MASK_DB ? 4 : 2;
    }

    std::uint64_t stackMask() const    
    {
        return sdesc_[SREG_SS].flags & SD_FLAGS_MASK_DB ? 0xffffffff : 0xffff;
    }

    std::uint64_t ipMask() const
    {
        return (uint64_t(1) << 8 * defaultOperandSize()) - 1;
    }
};

std::string FormatCPUFlags(uint32_t flags);
void ShowCPUState(const CPUState& state);

struct SegmentedAddress {
    SReg sreg;
    std::uint64_t offset;
};

inline bool operator==(const SegmentedAddress& l, const SegmentedAddress& r)
{
    return l.sreg == r.sreg && l.offset == r.offset;
}

class SystemBus;

class CPUHaltedException : public std::exception {
public:
    explicit CPUHaltedException() { }

    const char* what() const noexcept override {
        return "CPU halted with IF=0";
    }
};

class CPU : public CPUState {
public:
    using InterruptFunc = std::function<int ()>;

    explicit CPU(CPUModel cpuModel, SystemBus& bus)
        : cpuModel_ { cpuModel }
        , shiftMask_ { static_cast<uint8_t>(cpuModel <= CPUModel::i8086 ? 63 : 31) }
        , bus_ { bus } 
    {
        reset();
    }

    static constexpr size_t MaxHistory = 64;

    void reset();
    void setInterruptFunction(InterruptFunc func)
    {
        intFunc_ = func;
    }
    CPUInfo cpuInfo() const;
    uint64_t instructionsExecuted() const
    {
        return instructionsExecuted_;
    }
    bool halted() const
    {
        return halted_;
    }
    void trace();
    void showHistory(size_t max = MaxHistory);
    void step();
    Address currentIp() const;
    int lastExceptionNo() const;
    void loadSreg(SReg sr, std::uint16_t value);

    uint32_t exceptionTraceMask() const
    {
        return exceptionTraceMask_;
    }

    void exceptionTraceMask(uint32_t mask)
    {
        exceptionTraceMask_ = mask;
    }

    void clearHistory();

private:
    const CPUModel cpuModel_;
    const uint8_t shiftMask_;
    SystemBus& bus_;
    InterruptFunc intFunc_;
    InstructionDecodeResult currentInstruction;
    struct {
        CPUState state;
        uint8_t instructionBytes[MaxInstructionBytes];
        int exception;
    } history_[MaxHistory];
    size_t instructionsExecuted_;
    uint64_t currentIp_;
    bool halted_;
    uint32_t exceptionTraceMask_ = UINT32_MAX & ~(1 << 0); // #DE

    struct VerifiedAddress {
        SegmentedAddress addr;
        uint64_t physicalAddress;
        uint8_t size;
        bool valid;
        bool forWrite;
    };
    VerifiedAddress verifiedAddresses_[2];
    uint64_t verifyAddress(const SegmentedAddress& addr, uint8_t size, bool forWrite);

    std::uint8_t readCodeByte(uint64_t offset, bool peek);
    SegmentedAddress currentSp() const;

    void showState(const CPUState& state, const uint8_t* instructionBytes);

    void updateFlags(std::uint64_t value, std::uint64_t carry, std::uint32_t flagsMask);
    void setFlags(std::uint32_t value);

    std::uint64_t readEA(int index);
    void writeEA(int index, std::uint64_t value);
    SegmentedAddress calcAddress(const DecodedEA& ea) const;
    SegmentedAddress calcAddressNoMask(const DecodedEA& ea) const;
    Address readFarPtr(const DecodedEA& addrEa);

    std::uint64_t pageLookup(std::uint64_t linearAddress, std::uint32_t lookupFlags);
    std::uint64_t toLinearAddress(const SegmentedAddress& address, std::uint8_t accessSize) const;
    std::uint64_t toPhysicalAddress(const SegmentedAddress& address, std::uint8_t accessSize, std::uint32_t lookupFlags);

    std::uint64_t readMem(const SegmentedAddress& address, std::uint8_t size);
    std::uint64_t readMemLinear(std::uint64_t address, std::uint8_t size);
    std::uint64_t readMemPhysical(std::uint64_t address, std::uint8_t size);

    void writeMem(const SegmentedAddress& address, std::uint64_t value, std::uint8_t size);
    void writeMemPhysical(std::uint64_t address, std::uint64_t value, std::uint8_t size);

    SegmentDescriptor readDescriptor(std::uint16_t value);

    void push(std::uint64_t value, std::uint8_t size);
    std::uint64_t pop(std::uint8_t size);
    std::uint64_t readStack(std::int32_t itemOffset);
    void writeStack(std::int32_t itemOffset, std::uint64_t value);
    void updateSp(std::int32_t itemCount);

    enum class ControlTransferType { jump, call, interrupt, max };
    void doStep();
    void doControlTransfer(std::uint16_t cs, std::uint64_t ip, ControlTransferType type);
    void doNearControlTransfer(ControlTransferType type);
    void doInterrupt(std::uint8_t interruptNo, bool hardwareInterrupt);
    void doFarJump(std::uint16_t cs, std::uint64_t ip);
    void doInterruptReturn();

    std::uint64_t tssAddress(std::uint32_t limitCheck);
    void tssSaveStack();
    void tssRestoreStack(std::uint8_t newCpl);

    template<InstructionMnem>
    void doStringInstruction();

    template <InstructionMnem>
    void doBitInstruction();

    template<SReg>
    void doLoadFarPointer();
    
    void checkPriv(std::uint32_t errorCode);
    void checkSreg(std::uint8_t regNum);
};

constexpr bool Parity(uint8_t v) // Returns true if parity bit should be set
{
    return (!((0x6996 >> ((v ^ (v >> 4)) & 0xf)) & 1));
}

#endif
