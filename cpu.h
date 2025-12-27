#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <functional>
#include <optional>
#include <cassert>
#include "cpu_descriptor.h"
#include "cpu_registers.h"
#include "decode.h"

constexpr std::uint32_t CR0_BIT_PE = 0; // Protected Mode Enable
constexpr std::uint32_t CR0_BIT_WP = 16; // Write protect (CPU cannot write to R/O pages in ring 0)
constexpr std::uint32_t CR0_BIT_PG = 31; // Paging

constexpr std::uint32_t CR0_MASK_PE = 1U << CR0_BIT_PE; // Protected Mode Enable
constexpr std::uint32_t CR0_MASK_WP = 1U << CR0_BIT_WP; // Write protect
constexpr std::uint32_t CR0_MASK_PG = 1U << CR0_BIT_PG; // Paging

static constexpr std::uint32_t MaxPrefetchQueueLength = 16; // Keep power of two
struct PrefixFetchQueue {
    std::uint32_t putPos;
    std::uint32_t getPos;
    std::uint8_t data[MaxPrefetchQueueLength];
    std::uint64_t ip; // In reality the BIU increments IP and the CPU adjusts it accordingly... let's not do that for now

    std::uint32_t size() const
    {
        return putPos - getPos;
    }

    bool empty() const
    {
        return size() == 0;
    }

    void put(std::uint8_t byte)
    {
        data[(putPos++) & (MaxPrefetchQueueLength - 1)] = byte;
    }

    std::uint8_t get()
    {
        assert(!empty());
        return data[(getPos++) & (MaxPrefetchQueueLength - 1)];
    }

    std::uint8_t peek(std::uint32_t offset) const
    {
        return data[(getPos + offset) & (MaxPrefetchQueueLength - 1)];
    }

    void flush(std::uint64_t newIp)
    {
        putPos = getPos = 0;
        ip = newIp;
    }
};

constexpr uint32_t PT32_MASK_P = 1 << 0; // Present
constexpr uint32_t PT32_MASK_W = 1 << 1; // Writable
constexpr uint32_t PT32_MASK_U = 1 << 2; // User accessible (otherwise only for supervisor)
constexpr uint32_t PT32_MASK_A = 1 << 5; // Accessed
constexpr uint32_t PT32_MASK_D = 1 << 6; // Dirty (not for PDE)

constexpr uint32_t PDE32_MASK_PS = 1 << 7;
constexpr uint32_t PT32_MASK_ADDR = 0xfffff000; // Bit 31-12
constexpr uint32_t PAGE_MASK = ~PT32_MASK_ADDR; // Bit 11-0
static_assert(PAGE_MASK == 4095);
constexpr uint32_t PT32_SHIFT = 12;
constexpr uint32_t PAGE_SIZE = 1 << PT32_SHIFT;

static constexpr uint32_t TLB_MASK_W = 1 << 6; // Read/Write
static constexpr uint32_t TLB_MASK_U = 1 << 10; // User/Supervisor
static constexpr uint32_t TLB_MASK_D = 1 << 10; // Dirty
static constexpr uint32_t TLB_MASK_V = 1 << 11; // Valid

struct TLBEntry {
    uint32_t tag;
    uint32_t value;

    bool valid() const
    {
        return (value & TLB_MASK_V) != 0;
    }

    bool match(uint64_t linearAddress);
};

struct TLB {
    // Let's just use the 386 size for now
    static constexpr uint32_t SetWay = 4;
    static constexpr uint32_t SetSize = 8;

    TLBEntry tlb[SetSize][SetWay];

    TLBEntry* find(uint64_t linearAddress);
    TLBEntry* alloc(uint64_t linearAddress); // N.B. must not already be present
    void invalidate();
};

struct CPUState {
    std::uint64_t regs_[16];
    std::uint16_t sregs_[6];
    std::uint64_t cregs_[8];
    std::uint64_t dregs_[8];
    std::uint64_t ip_;
    std::uint32_t flags_;
    DescriptorTable idt_;
    DescriptorTable gdt_;
    SegmentDescriptor sdesc_[6];
    SegmentDescriptor ldt_;
    uint16_t ldtIndex_;
    SegmentDescriptor task_;
    uint16_t taskIndex_;
    PrefixFetchQueue prefetch_;
    TLB tlb_;
    bool intDelay_;

    bool protectedMode() const
    {
        return (cregs_[0] & CR0_MASK_PE) != 0;
    }

    bool pagingEnabled() const
    {
        return (cregs_[0] & (CR0_MASK_PE | CR0_MASK_PG)) == (CR0_MASK_PE | CR0_MASK_PG);
    }

    bool vm86() const;
    std::uint8_t iopl() const;

    std::uint8_t cpl() const
    {
        return sdesc_[SREG_CS].dpl();
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

    explicit CPU(CPUModel cpuModel, SystemBus& bus);

    static constexpr size_t MaxHistory = 256;

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
    void showControlTransferHistory(size_t max = maxControlTransferHistory);
    void step();
    Address currentIp() const;
    int lastExceptionNo() const;
    void loadSreg(SReg sr, std::uint16_t value);
    void setCreg(std::uint8_t index, std::uint32_t value);

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
    const uint8_t prefetchQueueLength_;
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

    static constexpr size_t maxControlTransferHistory = 64;
    struct {
        Address addr;
        Address destination;
        InstructionMnem ins;
        uint32_t count;
    } controlTransferHistory_[maxControlTransferHistory];
    size_t controlTransferHistoryCount_ = 0;

    // Instruction fetching
    void instructionPrefetch();
    bool instructionFetch(bool prefetch);

    SegmentedAddress currentSp() const;

    void showState(const CPUState& state, const uint8_t* instructionBytes);

    void updateFlags(std::uint64_t value, std::uint64_t carry, std::uint32_t flagsMask);
    void setFlags(std::uint32_t value);
    uint32_t filterFlags(std::uint32_t flags, bool op16bit);

    // EA
    std::uint64_t readEA(int index);
    void writeEA(int index, std::uint64_t value);
    SegmentedAddress calcAddress(const DecodedEA& ea) const;
    SegmentedAddress calcAddressNoMask(const DecodedEA& ea) const;

    // Paging
    std::uint64_t pageLookup(std::uint64_t linearAddress, std::uint32_t lookupFlags);

    // Physical access
    std::uint64_t readMemPhysical(std::uint64_t address, std::uint8_t size);
    void writeMemPhysical(std::uint64_t address, std::uint64_t value, std::uint8_t size);

    // Linear access
    std::uint64_t toPhysicalAddress(uint64_t linearAddress, std::uint32_t lookupFlags);
    std::uint64_t readMemLinear(std::uint64_t linearAddress, std::uint8_t size, uint32_t lookupFlags = 0);
    void writeMemLinear(std::uint64_t linearAddress, std::uint64_t value, std::uint8_t size, uint32_t lookupFlags = 0);
    
    // Logical access    
    std::uint64_t toLinearAddress(const SegmentedAddress& address, std::uint8_t size, bool forWrite);
    void writeMem(const SegmentedAddress& address, std::uint64_t value, std::uint8_t size);
    std::uint64_t readMem(const SegmentedAddress& address, std::uint8_t size);
    std::optional<std::uint64_t> peekMem(const SegmentedAddress& address, std::uint8_t size);

    // Memory access helpers
    std::uint64_t descriptorLinearAddress(std::uint16_t value);
    Address readFarPtr(const DecodedEA& addrEa);
    uint64_t readDescriptorValue(uint64_t linearAddress);
    SegmentDescriptor readDescriptor(std::uint16_t value);

    // Stack helpers
    void push(std::uint64_t value, std::uint8_t size = 0); // 0 = default operand size
    std::uint64_t pop(std::uint8_t size = 0); // 0 = default operand size
    std::uint64_t readStack(std::int32_t itemOffset);
    void writeStack(std::int32_t itemOffset, std::uint64_t value);
    void updateSp(std::int32_t itemCount);

    enum class ControlTransferType { jump, call, int32, int16, iret, retf, max };
    void doStep();
    void doControlTransfer(std::uint16_t cs, std::uint64_t ip, ControlTransferType type);
    void doNearControlTransfer(ControlTransferType type);
    void doInterrupt(int interrupt, std::uint32_t errorCode = 0);
    void doInterruptReturn();
    void doFarReturn(uint16_t bytesToPop);

    std::uint64_t tssAddress(std::uint32_t limitCheck);
    void tssSaveStack();
    void tssRestoreStack(std::uint8_t newCpl, bool fromVM86, std::uint8_t stackOpSize);

    void changeCpl(uint8_t newCpl);
    void clearSreg(SReg sr);
    void clearAllSregs();

    template<InstructionMnem>
    void doStringInstruction();

    template <InstructionMnem>
    void doBitInstruction();

    template<SReg>
    void doLoadFarPointer();
    
    void checkPriv(std::uint32_t errorCode);
    void checkPrivIOPL();
    void checkPrivVM86();
    void checkPmode();
    void checkIOAccess(std::uint16_t port, std::uint8_t size);

    void checkSreg(std::uint8_t regNum);

    void flushTLB();

    void recordControlTransfer(uint16_t cs, uint64_t ip);
    void checkIpLimit(uint16_t cs, uint64_t ip);
};

constexpr bool Parity(uint8_t v) // Returns true if parity bit should be set
{
    return (!((0x6996 >> ((v ^ (v >> 4)) & 0xf)) & 1));
}

#endif
