#include <print>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <cassert>
#include <memory>
#include <cstring>
#include "util.h"
#include "gzstream.h"
#include "cpu.h"
#include "cpu_exception.h"
#include "cpu_flags.h"
#include "system_bus.h"

// #include "address.h"
//#include "fileio.h"
//#include "debugger.h"
//#include "gui.h"
//#include "devs/cga.h"
//#include "devs/i8259a_pic.h"
//#include "devs/i8253_pit.h"
//#include "devs/nec765_floppy_controller.h"
//#include "devs/i8237a_dma_controller.h"

static std::string mooTestDir = "../../../misc/SingleStepTests/";

constexpr uint32_t MakeMooId(const char (&IdStr)[5])
{
    return static_cast<uint8_t>(IdStr[0]) | static_cast<uint8_t>(IdStr[1]) << 8 | static_cast<uint8_t>(IdStr[2]) << 16 | static_cast<uint8_t>(IdStr[3]) << 24;
}

std::string FormatMooId(uint32_t id)
{
    return std::format("{:c}{:c}{:c}{:c}", id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff, (id >> 24) & 0xff);
}

constexpr auto MOO_MOO = MakeMooId("MOO ");
constexpr auto MOO_META = MakeMooId("META");
constexpr auto MOO_TEST = MakeMooId("TEST");
constexpr auto MOO_NAME = MakeMooId("NAME");
constexpr auto MOO_BYTS = MakeMooId("BYTS");
constexpr auto MOO_HASH = MakeMooId("HASH");
constexpr auto MOO_INIT = MakeMooId("INIT");
constexpr auto MOO_FINA = MakeMooId("FINA");
constexpr auto MOO_CYCL = MakeMooId("CYCL");
constexpr auto MOO_EXCP = MakeMooId("EXCP");
constexpr auto MOO_REGS = MakeMooId("REGS");
constexpr auto MOO_RG32 = MakeMooId("RG32");
constexpr auto MOO_EA32 = MakeMooId("EA32");
constexpr auto MOO_RM32 = MakeMooId("RM32");
constexpr auto MOO_RAM = MakeMooId("RAM ");
constexpr auto MOO_QUEU = MakeMooId("QUEU");
constexpr auto MOO_GMET = MakeMooId("GMET"); // Generating metadata


enum {
    MOO_RG16_AX,
    MOO_RG16_BX,
    MOO_RG16_CX,
    MOO_RG16_DX,
    MOO_RG16_CS,
    MOO_RG16_SS,
    MOO_RG16_DS,
    MOO_RG16_ES,
    MOO_RG16_SP,
    MOO_RG16_BP,
    MOO_RG16_SI,
    MOO_RG16_DI,
    MOO_RG16_IP,
    MOO_RG16_FLAGS,
    MOO_RG16_MAX,
};
static_assert(MOO_RG16_MAX == 14);

static int MooRg16InvSregMap(SReg sr)
{
    constexpr int map[4] = {
        MOO_RG16_ES,
        MOO_RG16_CS,
        MOO_RG16_SS,
        MOO_RG16_DS,
    };
    return map[sr];
}

[[maybe_unused]] static Reg MooRg16RegMap(int index)
{
    switch (index) {
    case MOO_RG16_AX:
        return REG_AX;
    case MOO_RG16_BX:
        return REG_BX;
    case MOO_RG16_CX:
        return REG_CX;
    case MOO_RG16_DX:
        return REG_DX;
    case MOO_RG16_SI:
        return REG_SI;
    case MOO_RG16_DI:
        return REG_DI;
    case MOO_RG16_BP:
        return REG_BP;
    case MOO_RG16_SP:
        return REG_SP;
    default:
        throw std::runtime_error { std::format("{} is not a (normal) MOO 16-bit register", index) };
    }
}

static int MooRg16InvRegMap(Reg reg)
{
    constexpr int map[8] = {
        MOO_RG16_AX,
        MOO_RG16_CX,
        MOO_RG16_DX,
        MOO_RG16_BX,
        MOO_RG16_SP,
        MOO_RG16_BP,
        MOO_RG16_SI,
        MOO_RG16_DI,
    };
    return map[reg];
}

enum {
    MOO_RG32_CR0,
    MOO_RG32_CR3,
    MOO_RG32_EAX,
    MOO_RG32_EBX,
    MOO_RG32_ECX,
    MOO_RG32_EDX,
    MOO_RG32_ESI,
    MOO_RG32_EDI,
    MOO_RG32_EBP,
    MOO_RG32_ESP,
    MOO_RG32_CS,
    MOO_RG32_DS,
    MOO_RG32_ES,
    MOO_RG32_FS,
    MOO_RG32_GS,
    MOO_RG32_SS,
    MOO_RG32_EIP,
    MOO_RG32_EFLAGS,
    MOO_RG32_DR6,
    MOO_RG32_DR7,
    MOO_RG32_MAX,
};
static_assert(MOO_RG32_MAX == 20);

bool MooRg32IsSreg(int index)
{
    return index >= MOO_RG32_CS && index <= MOO_RG32_SS;
}

static SReg MooRg32SregMap(int index)
{
    assert(MooRg32IsSreg(index));
    constexpr SReg map[6] = { SREG_CS,
        SREG_DS,
        SREG_ES,
        SREG_FS,
        SREG_GS,
        SREG_SS };
    return map[index - MOO_RG32_CS];
}

static int MooRg32InvSregMap(SReg sr)
{
    constexpr int map[6] = {
        MOO_RG32_ES,
        MOO_RG32_CS,
        MOO_RG32_SS,
        MOO_RG32_DS,
        MOO_RG32_FS,
        MOO_RG32_GS,
    };
    return map[sr];
}

[[maybe_unused]] static Reg MooRg32RegMap(int index)
{
    switch (index) {
    case MOO_RG32_EAX:
        return REG_AX;
    case MOO_RG32_EBX:
        return REG_BX;
    case MOO_RG32_ECX:
        return REG_CX;
    case MOO_RG32_EDX:
        return REG_DX;
    case MOO_RG32_ESI:
        return REG_SI;
    case MOO_RG32_EDI:
        return REG_DI;
    case MOO_RG32_EBP:
        return REG_BP;
    case MOO_RG32_ESP:
        return REG_SP;
    default:
        throw std::runtime_error {std::format("{} is not a (normal) MOO 32-bit register", index)};
    }
}

static int MooRg32InvRegMap(Reg reg)
{
    constexpr int map[8] = {
        MOO_RG32_EAX,
        MOO_RG32_ECX,
        MOO_RG32_EDX,
        MOO_RG32_EBX,
        MOO_RG32_ESP,
        MOO_RG32_EBP,
        MOO_RG32_ESI,
        MOO_RG32_EDI,
    };
    return map[reg];
}

struct MooMeta {
    uint8_t majorVersion;
    uint8_t minorVersion;
    uint8_t cpuType;
    uint32_t opcode;
    char mnemonic[9];
    uint32_t testCount;
    uint64_t testSeed;
    uint8_t cpuMode; // 0 = realMode

    std::string description() const
    {
        int width = 2;
        if (opcode > 0xffffff)
            width = 8;
        else if (opcode > 0xffff)
            width = 6;
        else if (opcode > 0xff)
            width = 4;
        return std::format("{:0{}X} {}", opcode, width, mnemonic);
    }
};

struct MooMem {
    uint32_t address;
    uint8_t value;
};

struct MooRm32 {
    uint32_t regMask[MOO_RG32_MAX];
};

using MooRm32Ptr = std::unique_ptr<MooRm32>;

struct MooState {
    enum { RG_NONE,
        RG16,
        RG32 } regType;
    uint32_t regMask = RG_NONE;
    union {
        uint16_t rg16[MOO_RG16_MAX];
        uint32_t rg32[MOO_RG32_MAX];
    };
    std::vector<MooMem> mem;
    MooRm32Ptr masks;

    bool regActive(int index) const
    {
        return ((regMask >> index) & 1) != 0;
    }

    uint16_t readSreg(SReg sr) const {
        if (regType == RG16)
            return static_cast<uint16_t>(rg16[MooRg16InvSregMap(sr)]);
        assert(regType == RG32);
        return static_cast<uint16_t>(rg32[MooRg32InvSregMap(sr)]);
    };

    uint64_t readReg(Reg reg, int width) const {
        const auto mask = (1ULL << 8 * width) - 1;
        if (regType == RG16)
            return rg16[MooRg16InvRegMap(reg)] & mask;
        assert(regType == RG32);
        return rg32[MooRg32InvRegMap(static_cast<Reg>(reg & 7))] & mask;
    }

    uint64_t ip() const
    {
        return regType == RG16 ? rg16[MOO_RG16_IP] : rg32[MOO_RG32_EIP];
    }

    uint32_t flags() const
    {
        return regType == RG16 ? rg16[MOO_RG16_FLAGS] : rg32[MOO_RG32_EFLAGS];
    }

    uint8_t readU8(uint16_t srVal, uint32_t offset) const {
        auto physicalAddress = (srVal << 4) + offset;
        if (regType == RG16)
            physicalAddress &= 0xfffff;
        for (const auto& [addr, val] : mem) {
            if (addr == physicalAddress)
                return val;
        }
        throw std::runtime_error { std::format("Invalid read from address {:05X} ({:04X}:{:04X})", physicalAddress, srVal, offset) };
    };

    uint16_t readU16(uint16_t srVal, uint32_t offset) const {
        const auto lsb = readU8(srVal, offset);
        return lsb | readU8(srVal, offset + 1) << 8;
    };

    uint64_t read(uint16_t srVal, uint32_t offset, int size) const {
        if (size == 1)
            return readU8(srVal, offset);
        else if (size == 2)
            return readU16(srVal, offset);
        else
            throw std::runtime_error { std::format("TODO: MooSate::read size = {}", size) };
    };
};

struct MooTest {
    uint32_t id;
    std::string name;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> hash;
    MooState init;
    MooState fina;
    uint8_t exceptionNo;
    uint32_t flagsStackAddr; // =0 no exception
    MooRm32 const* masks;

    std::string hashString() const
    {
        return HexString(hash);
    }

    std::string instructionBytesString() const
    {
        return HexString(bytes);
    }

    MooState makeFinal() const
    {
        MooState finalState {};
        finalState.regType = fina.regType;
        if (finalState.regType == MooState::RG32) {
            for (int i = 0; i < MOO_RG32_MAX; ++i)
                finalState.rg32[i] = fina.regActive(i) ? fina.rg32[i] : init.rg32[i];
            finalState.regMask = (1 << MOO_RG32_MAX) - 1;
        } else {
            assert(finalState.regType == MooState::RG16);
            for (int i = 0; i < MOO_RG16_MAX; ++i)
                finalState.rg16[i] = fina.regActive(i) ? fina.rg16[i] : init.rg16[i];
            finalState.regMask = (1 << MOO_RG16_MAX) - 1;
        }

        finalState.mem = fina.mem;
        for (const auto& m : init.mem)
            finalState.mem.push_back(m);
        return finalState;
    }
};

const char* const MooRg16RegNames[MOO_RG16_MAX] = { "ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags" };
const char* const MooRg32RegNames[MOO_RG32_MAX] = {
    "CR0",
    "CR3",
    "EAX",
    "EBX",
    "ECX",
    "EDX",
    "ESI",
    "EDI",
    "EBP",
    "ESP",
    "CS",
    "DS",
    "ES",
    "FS",
    "GS",
    "SS",
    "EIP",
    "EFLAGS",
    "DR6",
    "DR7",
};

void PrintMooState(const MooState& st)
{
    if (st.regType == MooState::RG16) {
        for (int i = 0; i < MOO_RG16_MAX; ++i) {
            if (!st.regActive(i))
                continue;
            if (i != MOO_RG16_FLAGS) {
                std::println("{} = 0x{:04X} {} {}", MooRg16RegNames[i], st.rg16[i], st.rg16[i], static_cast<int16_t>(st.rg16[i]));
            } else {
                std::println("flags = {}", FormatCPUFlags(st.rg16[i]));
            }
        }
    } else {
        assert(st.regType == MooState::RG32);
        for (int i = 0; i < MOO_RG32_MAX; ++i) {
            if (!st.regActive(i))
                continue;
            if (i != MOO_RG32_EFLAGS) {
                std::println("{:3} = 0x{:08X} {} {}", MooRg32RegNames[i], st.rg32[i], st.rg32[i], static_cast<int32_t>(st.rg32[i]));
            } else {
                std::println("flags = {}", FormatCPUFlags(st.rg32[i]));
            }
        }
    }
    for (const auto [addr, val] : st.mem) {
        std::println("[{:06X}] = {:02X}", addr, val);
    }
}

class MooFile {
public:
    explicit MooFile(std::istream& in)
        : in_ { in }
    {
        if (const auto type = enterChunk(); type != MOO_MOO)
            throw std::runtime_error { std::format("Invalid MOO file (chunk id: 0x{:X} - {})", type, FormatMooId(type)) };
        [[maybe_unused]] const auto majorVer = read<uint8_t>();
        [[maybe_unused]] const auto minorVer = read<uint8_t>();
        (void)read<uint16_t>(); // Reserved
        [[maybe_unused]] const auto testCount = read<uint32_t>();
        [[maybe_unused]] const auto cpuId = read<uint32_t>();
        //std::println("MOO {}.{} {} tests for {}", majorVer, minorVer, testCount, FormatMooId(cpuId));
        exitChunk();

        for (;;) {
            const uint32_t type = peekNextChunkId();
            switch (type) {
            case MOO_META:
                readMetaChunk();
                break;
            case MOO_RM32:
                assert(regMask_ == nullptr);
                enterChunk();
                regMask_ = readRm32Chunk();
                exitChunk();
                break;
            case MOO_TEST:
                return;
            default:
                throw std::runtime_error { "TODO: Handle MOO header chunk " + FormatMooId(type) };
            }
        }
    }

    bool read(void* buffer, size_t size)
    {
        if (!in_.read(static_cast<char*>(buffer), size))
            return false;
        pos_ += size;
        return true;
    }

    bool readLittleEndian(void* buffer, size_t size)
    {
        // TODO: reverse bytes if big endian..
        return read(buffer, size);
    }

    template<typename T>
    T read()
    {
        T res;
        if (!readLittleEndian(&res, sizeof(res)))
            throw std::runtime_error { "Error reading from MOO file" };
        return res;
    }

    std::vector<uint8_t> readVector(size_t size)
    {
        std::vector<uint8_t> v(size);
        readOrDie(&v[0], v.size());
        return v;
    }

    std::vector<uint8_t> readLengthPrefixed()
    {
        return readVector(read<uint32_t>());
    }

    std::string readAciiz()
    {
        std::string s(read<uint32_t>(), ' ');
        readOrDie(&s[0], s.size());
        return s;
    }

    uint32_t enterChunk()
    {
        const uint32_t id = readNextChunkId();
        if (!id)
            return 0;
        uint32_t size = read<uint32_t>();
        ends_.push_back(pos_ + size);
        ids_.push_back(id);
        //std::println("{} Enter {} size={} end={}", pos_, pathString(), size, ends_.back());
        return id;
    }

    void exitChunk()
    {
        auto end = chunkEnd();
        ends_.pop_back();
        //std::println("{} Exit {} end={}", pos_, pathString(), end);
        if (pos_ > end)
            throw std::runtime_error { "Past end of chunk " + pathString() };
        ids_.pop_back();
        while (pos_ < end) {
            uint8_t buffer[256];
            readOrDie(buffer, std::min(sizeof(buffer), end - pos_));
        }
    }

    bool chunkDone() const
    {
        return pos_ == chunkEnd();
    }

    bool gotoNextTest()
    {
        const auto type = enterChunk();
        if (!type)
            return false;
        if (type != MOO_TEST)
            throw std::runtime_error { "TODO: Handle chunk type " + pathString() + "-" + FormatMooId(type) };
        return true;
    }

    MooTest readTestChunk()
    {
        assert(ids_.back() == MOO_TEST);
        MooTest test {};
        test.id = read<uint32_t>();
        while (!chunkDone()) {
            const auto type = enterChunk();
            if (type == MOO_NAME) {
                test.name = readAciiz();
            } else if (type == MOO_BYTS) {
                test.bytes = readLengthPrefixed();
            } else if (type == MOO_HASH) {
                test.hash = readVector(20);
            } else if (type == MOO_INIT || type == MOO_FINA) {
                readStateChunk(type == MOO_INIT ? test.init : test.fina);
            } else if (type == MOO_EXCP) {
                test.exceptionNo = read<uint8_t>();
                test.flagsStackAddr = read<uint32_t>();
                assert(test.flagsStackAddr != 0);
            } else if (type == MOO_CYCL) {
            } else if (type == MOO_GMET) {
            } else {
                throw std::runtime_error { std::format("TODO: Handle MOO {}", pathString()) };
            }
            exitChunk();
        }
        exitChunk();
        return test;
    }

    MooRm32Ptr readRm32Chunk()
    {
        assert(ids_.back() == MOO_RM32);
        const auto mask = read<uint32_t>();
        assert((mask >> MOO_RG32_MAX) == 0);
        auto rm32 = std::make_unique<MooRm32>();
        for (int i = 0; i < MOO_RG32_MAX; ++i) {
            if ((mask >> i) & 1) {
                rm32->regMask[i] = read<uint32_t>();
            } else {
                rm32->regMask[i] = UINT32_MAX;
            }
        }
        return rm32;
    }

    uint32_t peekNextChunkId()
    {
        return fillNextChunkId();
    }

    const MooRm32* regMask() const
    {
        return regMask_.get();
    }

    const MooMeta& meta() const
    {
        return meta_;
    }

private:
    std::istream& in_;
    std::size_t pos_ = 0;
    std::vector<uint32_t> ids_;
    std::vector<std::size_t> ends_;
    uint32_t nextChunkId_ = 0;
    MooRm32Ptr regMask_;
    MooMeta meta_ {};
    
    uint32_t readNextChunkId()
    {
        fillNextChunkId();
        return std::exchange(nextChunkId_, 0);
    }

    uint32_t fillNextChunkId()
    {
        if (!nextChunkId_) {
            if (!readLittleEndian(&nextChunkId_, sizeof(nextChunkId_))) {
                if (in_.gcount() == 0)
                    return nextChunkId_ = 0;
                throw std::runtime_error { "Error reading from MOO file " + pathString() };
            }
        }
        return nextChunkId_;
    }

    std::string pathString() const
    {
        std::string res {};
        for (size_t i = 0; i < ids_.size(); ++i) {
            if (i)
                res += '-';
            res += FormatMooId(ids_[i]);
        }
        return res;
    }

    size_t chunkEnd() const
    {
        if (ends_.empty())
            throw std::runtime_error { "No chunk active" };
        return ends_.back();
    }

    void readOrDie(void* buffer, size_t size)
    {
        if (!read(buffer, size) || !in_)
            throw std::runtime_error { "Error reading from MOO file " + pathString() };
    }

    template<typename I>
    void read(I& value)
    {
        value = read<I>();
    }

    void readStateChunk(MooState& state)
    {
        while (!chunkDone()) {
            const auto type = enterChunk();
            if (type == MOO_REGS) {
                assert(state.regType == MooState::RG_NONE);
                state.regType = MooState::RG16;
                state.regMask = read<uint16_t>();
                assert((state.regMask >> MOO_RG16_MAX) == 0);
                for (unsigned int i = 0; i < MOO_RG16_MAX; ++i) {
                    if (state.regActive(i))
                        state.rg16[i] = read<uint16_t>();
                }
            } else if (type == MOO_RG32) {
                assert(state.regType == MooState::RG_NONE);
                state.regType = MooState::RG32;
                state.regMask = read<uint32_t>();
                assert((state.regMask >> MOO_RG32_MAX) == 0);
                for (unsigned int i = 0; i < MOO_RG32_MAX; ++i) {
                    if (state.regActive(i))
                        state.rg32[i] = read<uint32_t>();
                }
            } else if (type == MOO_EA32) {
            } else if (type == MOO_RAM) {
                const auto count = read<uint32_t>();
                assert(state.mem.empty());
                state.mem.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                    auto& m = state.mem[i];
                    m.address = read<uint32_t>();
                    m.value = read<uint8_t>();
                }
            } else if (type == MOO_QUEU) {
            } else {
                std::println("TODO: Handle MOO {}", pathString());
                exit(1);
            }
            exitChunk();
        }
        assert(state.regType != MooState::RG_NONE);
    }

    void readMetaChunk()
    {
        assert(nextChunkId_ == MOO_META);
        enterChunk();
        read(meta_.majorVersion);
        read(meta_.minorVersion);
        read(meta_.cpuType);
        read(meta_.opcode);
        readOrDie(meta_.mnemonic, 8);
        read(meta_.testCount);
        read(meta_.testSeed);
        read(meta_.cpuMode);

        exitChunk();
    }
};

class MooTestMachine : public MemoryHandler, public IOHandler {
public:
    explicit MooTestMachine(CPUModel cpuModel)
        : cpu_ {cpuModel, bus_}
    {
        const auto memSize = cpuModel >= CPUModel::i80386sx ? 0x200000 : 0x100000; // 2MB / 1MB
        bus_.setDefaultIOHandler(this);
        bus_.setAddressMask(memSize - 1);
        bus_.addMemHandler(0, memSize, *this);
        cpu_.exceptionTraceMask(0);
    }

    CPU& cpu()
    {
        return cpu_;
    }

    void runTest(const MooTest& test, uint32_t ignoredFlagsMask = 0)
    {
        assert(test.init.regType == test.fina.regType && test.init.regType != MooState::RG_NONE);
        test_ = &test;
        ignoredFlags_ = ignoredFlagsMask;
        writes_.clear();
        cpu_.reset();
        if (test.init.regType == MooState::RG16) {
            for (int i = 0; i < MOO_RG16_MAX; ++i) {
                if (test.init.regActive(i))
                    reg16(i) = test.init.rg16[i];
            }
        } else {
            assert(test.init.regType == MooState::RG32);
            for (int i = 0; i < MOO_RG32_MAX; ++i) {
                if (!test.init.regActive(i))
                    continue;
                const auto val = test.init.rg32[i];
                if (MooRg32IsSreg(i)) {
                    assert(val >> 16 == 0);
                    cpu_.loadSreg(MooRg32SregMap(i), static_cast<uint16_t>(val));
                } else {
                    reg32(i) = val;
                }
            }
        }
        cpu_.prefetch_.flush(cpu_.ip_ & cpu_.ipMask());

        if (cpu_.cpuInfo().model == CPUModel::i80386sx) {
            int exceptionNo = ExceptionNone;
            for (int i = 0;; ++i) {
                if (i == 3)
                    throw std::runtime_error { "Too many instructions executed" };
                try {
                    cpu_.step();
                } catch (const CPUHaltedException&) {
                    ++cpu_.ip_;
                    //++cpu_.ip_;
                    break;
                }
                if (cpu_.halted())
                    break;
                exceptionNo = cpu_.lastExceptionNo(); // Preserve
            }

            if (test.flagsStackAddr) {
                if (exceptionNo == ExceptionNone)
                    throw std::runtime_error { "Expected " + FormatExceptionNumber(test.exceptionNo) };
                else if ((exceptionNo & ExceptionNumberMask) != test.exceptionNo)
                    throw std::runtime_error { std::format("Wrong exception generated {} expected {}", FormatExceptionNumber(exceptionNo), FormatExceptionNumber(test.exceptionNo)) };
            } else if (exceptionNo != ExceptionNone) {
                throw std::runtime_error { std::format("Unexpected CPU exception: ", FormatExceptionNumber(exceptionNo)) };
            }
        } else {
            cpu_.step();
        }

        if (test.init.regType == MooState::RG16) {
            for (int i = 0; i < MOO_RG16_MAX; ++i) {
                const auto val = reg16(i);
                const auto expected = test.fina.regActive(i) ? test.fina.rg16[i] : test.init.rg16[i];
                if (val == expected)
                    continue;
                if (i == MOO_RG16_FLAGS) {
                    const auto flagDiff = (val ^ expected) & ~ignoredFlagsMask;
                    if (flagDiff == 0 || (cpu_.cpuInfo().model == CPUModel::i8088 && cpu_.lastExceptionNo() == (CPUExceptionNumber::DivisionError | ExceptionTypeCPU)))
                        continue;
                    throw std::runtime_error { std::format("Invalid value for flags {} expected {}, difference {} (ignored mask {:04X})", FormatCPUFlags(val), FormatCPUFlags(expected), FormatCPUFlags(flagDiff), ignoredFlagsMask) };
                } else {
                    throw std::runtime_error { std::format("Invalid value for {} {:04X} expected {:04X}", MooRg16RegNames[i], val, expected) };
                }
            }
        } else {
            for (int i = 0; i < MOO_RG32_MAX; ++i) {
                const auto val = readReg32(i);
                const auto expected = test.fina.regActive(i) ? test.fina.rg32[i] : test.init.rg32[i];
                if (val == expected)
                    continue;
                if (test.masks && ((val ^ expected) & test.masks->regMask[i]) == 0)
                    continue;
                if (i == MOO_RG32_EFLAGS) {
                    const auto flagDiff = (val ^ expected) & ~ignoredFlagsMask;
                    if (flagDiff == 0)
                        continue;
                    throw std::runtime_error { std::format("Invalid value for flags {} expected {}, difference {} (ignored mask {:04X})", FormatCPUFlags(val), FormatCPUFlags(expected), FormatCPUFlags(flagDiff), ignoredFlagsMask) };
                } else {
                    throw std::runtime_error { std::format("Invalid value for {} {:08X} expected {:08X}", MooRg32RegNames[i], val, expected) };
                }
            }
        }

        for (const auto [addr, val] : test.fina.mem) {
            auto m = findMem(writes_, addr);
            if (!m)
                throw std::runtime_error { std::format("Write to {:05X} not done. Expected {:02X}", addr, val) };
            assert(m->value == val); // Checked by writeU8
        }
    }

    std::uint8_t peekU8(std::uint64_t addr, [[maybe_unused]] std::uint64_t offset) override
    {
        try {
            return readU8(addr, offset);
        } catch (...) {
            return 0xCC;
        }
    }

    std::uint8_t readU8(std::uint64_t addr, [[maybe_unused]] std::uint64_t offset) override
    {
        if (auto m = findMem(test_->init.mem, addr); m)
            return m->value;
        // ENTER (c8) test actually needs to read written data
        if (auto m = findMem(writes_, addr); m)
            return m->value;

        // Instruction prefetch may do "invalid" reads
        //throw std::runtime_error { std::format("MOO: Invalid readU8 of address {:05X}", addr) };
        //std::println("MOO: FIXME Invalid readU8 of address {:05X}", addr);
        return 0xCC;
    }
    
    void writeU8(std::uint64_t addr, [[maybe_unused]] std::uint64_t offset, std::uint8_t value) override
    {
        auto m = findMem(test_->fina.mem, addr);
        if (!m) {
            m = findMem(test_->init.mem, addr);
            if (m) {
                if (m->value != value)
                    throw std::runtime_error { std::format("Unexpected write to {:05X} value {:02X} expected unchanged {:02X}", addr, value, m->value) };
                return;
            }
            throw std::runtime_error { std::format("Unexpected write to {:05X} value {:02X}", addr, value) };
        }
        if (m->value != value) {
            auto msg = std::format("Unexpected write to {:05X} value {:02X} expected {:02X}", addr, value, m->value);

            // Hack to ignore flags on division error
            if (cpu_.cpuInfo().model == CPUModel::i8088 && cpu_.lastExceptionNo() == (CPUExceptionNumber::DivisionError | ExceptionTypeCPU)) {
                const auto writeIdx = m - &test_->fina.mem[0];
                if (writeIdx < 2) {
                    const auto ignore = ignoredFlags_ | EFLAGS_MASK_PF | EFLAGS_MASK_ZF; // Always ignore ZF/PF on exception..
                    const auto diff = (m->value ^ value) & ~(ignore >> 8 * writeIdx);
                    if (!diff) {
                        //std::println("HACK for division error flags: {}", msg);
                        writes_.push_back(MooMem { static_cast<uint32_t>(addr), m->value });
                        return;
                    }
                    msg += std::format(" diff {}", FormatCPUFlags((((m->value ^ value) << 8 * writeIdx)) & ~ignore));
                }
            } else if (cpu_.cpuInfo().model == CPUModel::i80386sx && (cpu_.lastExceptionNo() & ExceptionTypeCPU) && test_->flagsStackAddr) {
                const auto writeIdx = addr - test_->flagsStackAddr;
                if (writeIdx < 2) {
                    const auto ignore = ignoredFlags_ | (test_->masks ? ~test_->masks->regMask[MOO_RG32_EFLAGS] : 0);
                    const auto diff = (m->value ^ value) & ~(ignore >> 8 * writeIdx);
                    if (!diff) {
                        writes_.push_back(MooMem { static_cast<uint32_t>(addr), m->value });
                        return;
                    }
                    msg += std::format(" diff {}", FormatCPUFlags((((m->value ^ value) << 8 * writeIdx)) & ~ignore));
                }

            }
            if (std::strstr(test_->name.c_str(), "enter")) {
                // HACK: ENTER writes to the same location multiple times
                std::println("ENTER Warning {}", msg);
                return;
            }
            throw std::runtime_error { msg };
        }
        writes_.push_back(MooMem { static_cast<uint32_t>(addr), value });
    }

    std::uint8_t inU8([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset) override
    {
        if (cpu_.cpuInfo().model == CPUModel::i80386sx) {
            // Looks like port 22h returns 7Fh and 23h 42h
            //"4fb5d80f331625dd650d55e8a1ab9d1da3b38784", // e5.MOO.gz 422 in ax,21h failed -- Invalid value for EAX 6F41FFFF expected 6F417FFF
            //"29c9c6b39824411334d44d57db62504bb4807fc6", // 66e5.MOO.gz 190 in eax,1Fh failed -- Invalid value for EAX FFFFFFFF expected 7FFFFFFF
            //"ab010dbcc86182e4ce40933f61f0864ddfd38bab", // 66e5.MOO.gz 254 in eax,1Fh failed -- Invalid value for EAX FFFFFFFF expected 7FFFFFFF
            //"f9d9686381f6845b06163938406074037c1768a2", // 66e5.MOO.gz 340 in eax,1Fh failed -- Invalid value for EAX FFFFFFFF expected 7FFFFFFF
            //"c923d58b0eca0d62696e03e56c9fd46ae645bee6", // 66e5.MOO.gz 348 in eax,1Fh failed -- Invalid value for EAX FFFFFFFF expected 7FFFFFFF
            //"62f9cffa058135d552793d2e2505fc93e353ffad", // 66e5.MOO.gz 422 in eax,21h failed -- Invalid value for EAX FFFFFFFF expected FF427FFF
            if (port == 0x22)
                return 0x7F;
            if (port == 0x23)
                return 0x42;
        }
        return 0xFF;
    }

    void outU8([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset, [[maybe_unused]] std::uint8_t value) override
    {
    }

private:
    SystemBus bus_;
    CPU cpu_;
    const MooTest* test_ = nullptr;
    std::vector<MooMem> writes_;
    uint32_t ignoredFlags_ = 0;

    static const MooMem* findMem(const std::vector<MooMem>& mem, uint64_t addr)
    {
        for (const auto&m : mem) {
            if (m.address == addr)
                return &m;
        }
        return nullptr;
    }

    std::uint16_t& reg16_normal(int index)
    {
        return reinterpret_cast<uint16_t&>(cpu_.regs_[index]);
    }

    std::uint32_t& reg32_normal(int index)
    {
        return reinterpret_cast<uint32_t&>(cpu_.regs_[index]);
    }

    std::uint16_t& reg16(int index)
    {
        switch (index) {
        case 0:
            return reg16_normal(REG_AX);
        case 1:
            return reg16_normal(REG_BX);
        case 2:
            return reg16_normal(REG_CX);
        case 3:
            return reg16_normal(REG_DX);
        case 4:
            return cpu_.sregs_[SREG_CS];
        case 5:
            return cpu_.sregs_[SREG_SS];
        case 6:
            return cpu_.sregs_[SREG_DS];
        case 7:
            return cpu_.sregs_[SREG_ES];
        case 8:
            return reg16_normal(REG_SP);
        case 9:
            return reg16_normal(REG_BP);
        case 10:
            return reg16_normal(REG_SI);
        case 11:
            return reg16_normal(REG_DI);
        case 12:
            return reinterpret_cast<uint16_t&>(cpu_.ip_);
        case MOO_RG16_FLAGS:
            return reinterpret_cast<uint16_t&>(cpu_.flags_);
        default:
            throw std::runtime_error { std::format("Invalid 16-bit register index {}", index) };
        }
    }

    std::uint32_t readReg32(int index)
    {
        return MooRg32IsSreg(index) ? cpu_.sregs_[MooRg32SregMap(index)] : reg32(index);
    }

    uint32_t fakeDr6 = 0;
    uint32_t fakeDr7 = 0;
    std::uint32_t& reg32(int index)
    {
        switch (index) {
        case MOO_RG32_CR0:
            return reinterpret_cast<uint32_t&>(cpu_.cregs_[0]);
        case MOO_RG32_CR3:
            return reinterpret_cast<uint32_t&>(cpu_.cregs_[3]);
        case MOO_RG32_EAX:
            return reg32_normal(REG_AX);
        case MOO_RG32_EBX:
            return reg32_normal(REG_BX);
        case MOO_RG32_ECX:
            return reg32_normal(REG_CX);
        case MOO_RG32_EDX:
            return reg32_normal(REG_DX);
        case MOO_RG32_ESI:
            return reg32_normal(REG_SI);
        case MOO_RG32_EDI:
            return reg32_normal(REG_DI);
        case MOO_RG32_EBP:
            return reg32_normal(REG_BP);
        case MOO_RG32_ESP:
            return reg32_normal(REG_SP);
        case MOO_RG32_EIP:
            return reinterpret_cast<uint32_t&>(cpu_.ip_);
        case MOO_RG32_EFLAGS:
            return cpu_.flags_;
        case MOO_RG32_DR6:
            return fakeDr6;
        case MOO_RG32_DR7:
            return fakeDr7;
        default:
            throw std::runtime_error { std::format("Invalid 32-bit register index {}", index) };
        }
    }
};

#include <filesystem>
#include <set>
#include <map>

template <typename F>
static void ForAllMooFiles(const std::string& path, const F& f)
{
    for (auto const& dir_entry : std::filesystem::directory_iterator { path }) {
        auto ps = dir_entry.path().filename().string();
        for (auto& c : ps) {
            if (c >= 'A' && c <= 'Z')
                c |= 0x20;
        }
        const char extension[] = ".moo.gz";
        if (!ps.ends_with(extension))
            continue;
        ps.erase(ps.end() - (sizeof(extension) - 1), ps.end());
        f(ps, dir_entry.path().string());
    }
}

static const std::set<std::string> blacklist {
    "8abbbc61a5866292b0bc816660d7b334bea7962a", //666f.MOO.gz 253 repne outsd -- looks like address bit 20 is ignored in this one???
};

[[maybe_unused]] static MooMeta ReadTestMetaData(const char* filename)
{
    GZInputStream gz { filename };
    MooFile moo { gz };
    return moo.meta();
}

static void PrintTestInfo(const MooTest& test)
{
    std::println("");
    std::println("Instruction Bytes: {}", test.instructionBytesString());
    std::println("");
    std::println("Initial state:");
    PrintMooState(test.init);
    std::println("");
    std::println("Expected final state:");
    PrintMooState(test.fina);
    std::println("");
}

std::string mooTestDescription;

static void TestMooFile(MooTestMachine& machine, const std::string& filename, uint32_t ignoredFlagsMask = 0, std::function<bool (MooTest&)> filter = nullptr)
{
    std::print("{}        \r", filename);
    GZInputStream gz { filename.c_str() };
    MooFile moo { gz };
    while (moo.gotoNextTest()) {
        auto test = moo.readTestChunk();
        if (!test.masks)
            test.masks = moo.regMask();
        if (blacklist.find(test.hashString()) != blacklist.end() || (filter && !filter(test)))
            continue;
        try {
            mooTestDescription = std::format("{} {} {}", test.hashString(), strrchr(filename.c_str(), '/') + 1, test.id);
            machine.runTest(test, ignoredFlagsMask);
        } catch ([[maybe_unused]] const std::exception& e) {
            PrintTestInfo(test);
            machine.cpu().showHistory();
            ShowCPUState(machine.cpu());
            std::println("{:04X}:{:04X}", machine.cpu().sregs_[SREG_CS], machine.cpu().ip_);
            std::println("");
            if (test.flagsStackAddr)
                std::println("Expected exception {} with flags at {:08X}\n", test.exceptionNo, test.flagsStackAddr);
            if (test.masks) {
                std::println("NB test mask is present!");
                for (int i = 0; i < MOO_RG32_MAX; ++i) {
                    if (test.masks->regMask[i] != UINT32_MAX)
                        std::println("Mask for {} = {:08X}", MooRg32RegNames[i], test.masks->regMask[i]);
                }
            }
            std::println("Test {} {} {} failed ({})", filename, test.id, test.name, test.hashString());
            std::println("{}", e.what());
            throw;
        }
    }
}

static void RunTestsInDir(CPUModel model, const std::string& path, const std::set<std::string>& ignoredTests, const std::map<std::string, uint32_t>& ignoredFlags)
{
    constexpr bool checkIgnored = false;
    [[maybe_unused]] std::set<std::string> passingIgnoredTests, failedButNotIgnored;
    [[maybe_unused]] std::map<std::string, MooMeta> testMeta;

    MooTestMachine machine { model };
    int skipped = 0, passed = 0;

    ForAllMooFiles(path, [&](const auto& testName, const auto& filename) {
        if constexpr (!checkIgnored) {
            if (ignoredTests.find(testName) != ignoredTests.end()) {
                ++skipped;
                return;
            }
        }
        uint32_t ignoreFlags = 0;
        if (auto it = ignoredFlags.find(testName); it != ignoredFlags.end())
            ignoreFlags = it->second;
        if constexpr (checkIgnored)
            testMeta[testName] = ReadTestMetaData(filename.c_str());
        try {
            TestMooFile(machine, filename.c_str(), ignoreFlags);
            ++passed;
            if constexpr (checkIgnored) {
                if (ignoredTests.find(testName) != ignoredTests.end())
                    passingIgnoredTests.insert(testName);
            }
        } catch (...) {
            if constexpr (checkIgnored) {
                if (ignoredTests.find(testName) == ignoredTests.end())
                    failedButNotIgnored.insert(testName);
                ++skipped;
            } else {
                throw;
            }
        }
    });
    std::println("{}: {}/{} tests pass ({} skipped)", path, passed, passed+skipped, skipped);
    if constexpr (checkIgnored) {
        if (!passingIgnoredTests.empty())
            std::println("Passing but ignored:");
        for (const auto& t : passingIgnoredTests) {
            std::println("{} doesn't need to be ignored! -- {}", t, testMeta[t].description());
        }
        if (!failedButNotIgnored.empty())
            std::println("Failing but not ignored:");
        for (const auto& t : failedButNotIgnored) {
            std::println("\"{}\", // {}", t, testMeta[t].description());
        }
        if (!passingIgnoredTests.empty() || !failedButNotIgnored.empty())
            throw std::runtime_error { "Too many ignored tests" };
        //std::println("Still ignored:");
        //for (const auto& t : ignoredTests) {
        //    std::println("\"{}\", // {}", t, testMeta[t].description());
        //}
    }
}

std::pair<uint16_t, uint64_t> MooCalcEaAddress(const MooState& state, const InstructionDecodeResult& ins, int idx)
{
    const auto& ea = ins.ea[idx];
    assert(ea.type == DecodedEAType::rm16);
    const auto mod = ModrmMod(ea.rm);
    const auto rm = ModrmRm(ea.rm);
    uint64_t offset;
    SReg segment = SREG_DS;
    if (mod == 0b00 && rm == 0b110) {
        offset = ea.disp & 0xffff;
    } else {
        constexpr Reg baseReg[8] = { REG_BX, REG_BX, REG_BP, REG_BP, REG_SI, REG_DI, REG_BP, REG_BX };
        constexpr Reg indexReg[4] = { REG_SI, REG_DI, REG_SI, REG_DI };
        if (baseReg[rm] == REG_BP)
            segment = SREG_SS;
        offset = state.readReg(baseReg[rm], 2);
        if (rm < 4)
            offset += state.readReg(indexReg[rm], 2);
        if (mod == 0b01)
            offset += static_cast<int8_t>(ea.disp & 0xff);
        else if (mod == 0b10)
            offset += static_cast<int16_t>(ea.disp & 0xffff);
    }
    offset &= ins.addressMask();
    if (ins.prefixes & PREFIX_SEG_MASK)
        segment = static_cast<SReg>(((ins.prefixes & PREFIX_SEG_MASK) >> PREFIX_SEG_SHIFT) - 1);
    return { state.readSreg(segment), offset };
}

uint64_t MooEaValue(const MooState& state, const InstructionDecodeResult& ins, int idx)
{
    const auto& ea = ins.ea[idx];

    switch (ea.type) {
    case DecodedEAType::reg8: {
        auto reg = state.readReg(static_cast<Reg>(ea.regNum & 3), 2);
        if (ea.regNum & 4)
            return reg >> 8;
        else
            return reg & 0xff;
    }
    case DecodedEAType::reg16:
        return state.readReg(static_cast<Reg>(ea.regNum), 2);
    case DecodedEAType::reg32:
        return state.readReg(static_cast<Reg>(ea.regNum), 4);
    case DecodedEAType::imm8:
        return SignExtend(ea.immediate, 1);
    case DecodedEAType::imm16:
        return SignExtend(ea.immediate, 2);
    case DecodedEAType::imm32:
        return SignExtend(ea.immediate, 4);
    case DecodedEAType::rm16: {
        auto addr = MooCalcEaAddress(state, ins, idx);
        return state.read(addr.first, static_cast<uint32_t>(addr.second), ins.operandSize);
    }
    default:
        throw std::runtime_error { std::format("TODO: MooEaValue for {}", ea.type) };
    }
}

struct MooDecodedInstruction {
    InstructionDecodeResult ins;
    std::string desc;
    uint64_t eaVal[MaxInstructionOperands];
};

[[maybe_unused]] static MooDecodedInstruction MooDecodeInstruction(const MooTest& test, const CPUInfo& cpuInfo)
{
    MooDecodedInstruction res {};
    int offset = 0;
    res.ins = Decode(cpuInfo, [&]() {
        return test.bytes[offset++];
    });
    const auto addr = Address { test.init.readSreg(SREG_CS), test.init.ip(), cpuInfo.defaultOperandSize };
    res.desc = FormatDecodedInstruction(res.ins, addr);

    for (int i = 0; i < res.ins.numOperands; ++i)
        res.eaVal[i] = MooEaValue(test.init, res.ins, i);

    return res;
}

static void TestMoo()
{
    constexpr auto imulUndefinedFlags = EFLAGS_MASK_PF | EFLAGS_MASK_AF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF;
    constexpr auto divUndefinedFlags = EFLAGS_MASK_PF | EFLAGS_MASK_AF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_OF | EFLAGS_MASK_CF;
    constexpr auto bitScanUndefinedFlags = EFLAGS_MASK_OF | EFLAGS_MASK_SF | EFLAGS_MASK_AF | EFLAGS_MASK_PF | EFLAGS_MASK_CF;
    constexpr auto rotUndefinedFlags = EFLAGS_MASK_AF;

    // TODO: v1_ex_real_mode/6766A5.MOO.gz 442 fails due to use of "SMC"
    auto m = MooTestMachine { CPUModel::i80386sx };
    m.cpu().exceptionTraceMask(UINT32_MAX);

#if 0
    m.cpu().exceptionTraceMask(0);

    struct ShiftRes {
        uint8_t shiftCount;
        uint32_t input;
        uint8_t carry;
        uint8_t overflow;
    };
    std::vector<ShiftRes> shiftRes;

    std::map<int, int> rMap;

    TestMooFile(m, "../misc/SingleStepTests/386/v1_ex_real_mode/0faf.MOO.gz", 0, [&](MooTest& test) {
        if (test.flagsStackAddr)
            return false;

        assert(test.init.regType == MooState::RG32);
        const auto decodedIns = MooDecodeInstruction(test, m.cpu().cpuInfo());
        const auto finalState = test.makeFinal();

        const auto m1 = decodedIns.eaVal[0]; // multiplier
        const auto m2 = decodedIns.eaVal[1]; // multiplicand
        //const auto product = m1 * m2;
        const auto expectedProduct = MooEaValue(finalState, decodedIns.ins, 0);

        //if (m1 != 0 && m2 != false)
        //    return false; // XXX

        auto x = m2, y = m1;
        bool flipSign = false;
        auto makePos = [&flipSign](uint64_t& num) {
            auto x = static_cast<int16_t>(num & 0xffff);
            if (x < 0) {
                flipSign = !flipSign;
                num = (-x & 0xffff);
            }
        };

        makePos(x);
        makePos(y);

        uint32_t product = 0;
        uint32_t flags = test.init.flags() & ~(EFLAGS_MASK_OF | EFLAGS_MASK_SF | EFLAGS_MASK_ZF | EFLAGS_MASK_AF | EFLAGS_MASK_PF | EFLAGS_MASK_CF);
        if (m1) {
            int nb = 0;
            while (m1 >> nb)
                nb++;

            for (int i = 0; i < nb; ++i) {
                if (y & 1)
                    product += (uint32_t)x;
                x <<= 1;
                y = (y << 15 | y >> 1) & 0xffff;
            }

            if (flipSign)
                product = -static_cast<int32_t>(product);
            if ((SignExtend(product, 2) ^ product) & 0xffffffff)
                flags |= EFLAGS_MASK_OF | EFLAGS_MASK_CF;
        } else {
            flags |= EFLAGS_MASK_ZF | EFLAGS_MASK_PF;
        }

        if ((product & 0xffff) != expectedProduct) {
            std::println("{:04X} * {:04X} = {:04X}`{:04X}, expected {:04X}", m1, m2, product >> 16, product & 0xffff, expectedProduct);
            exit(1);
        }

        auto flagMask = EFLAGS_MASK_OF | EFLAGS_MASK_CF | EFLAGS_MASK_ZF/* | EFLAGS_MASK_SF*/;

        if (product == 0) {
            flagMask |= EFLAGS_MASK_SF;
            if (m1 & 0x8000)
                flags |= EFLAGS_MASK_SF;
        }

        const auto flagDiff = finalState.flags() ^ flags;

        auto printDiff = [&]() {
            std::println("{:04X} * {:04X} = {:04X}`{:04X}, flags {} expected {} diff {} -- x={:04X}`{:04X} y={:04X} changed {}", m1, m2, product >> 16, product & 0xffff, FormatCPUFlags(flags), FormatCPUFlags(finalState.flags()), FormatCPUFlags(flagDiff), x >> 16, x & 0xffff, y, FormatCPUFlags(test.init.flags() ^ finalState.flags()));
        };
        if (flagDiff & flagMask) {
            printDiff();
            std::println("Exiting due to flag diff {}", FormatCPUFlags(flagDiff & flagMask));
            exit(1);
        }

        if (product && (finalState.flags() && EFLAGS_MASK_SF))
            printDiff();

        return false;
    });

    for (const auto& [idx, val] : rMap) {
        std::println("{:04b} = {}", idx, val);
    }

#if 0
    std::sort(shiftRes.begin(), shiftRes.end(), [](const auto& l, const auto& r) { return l.shiftCount < r.shiftCount; });
    for (const auto& sr : shiftRes) {
        if (sr.shiftCount == 1)
            std::println("{:2d} {:016b} {}", sr.shiftCount, sr.input, sr.overflow);
    }
#endif

#else
    //m.cpu().exceptionTraceMask(0);
    //TestMooFile(m, "../misc/SingleStepTests/386/v1_ex_real_mode/6669.MOO.gz", 0, [](const auto& test) {
    //    if (test.id != 1234)
    //        return false;
    //    PrintTestInfo(test);
    //    return true;
    //});
    //TestMooFile(m, "../misc/SingleStepTests/386/v1_ex_real_mode/0faf.MOO.gz", EFLAGS_MASK_PF | EFLAGS_MASK_AF);
#endif

#if 0
    auto m8088 = MooTestMachine { CPUModel::i8088 };
    //TestMooFile(m8088, "../misc/SingleStepTests/8088/f7.5.MOO.gz");

    TestMooFile(m8088, "../misc/SingleStepTests/8088/f6.4.MOO.gz", 0, [&](MooTest& test) {
#if 0
        const auto inFlags = test.init.rg16[MOO_RG16_FLAGS];
        const uint8_t inAL = test.init.rg16[MOO_RG16_AX] & 0xff;
        const uint8_t inAF = !!(inFlags & EFLAGS_MASK_AF);
        const uint8_t inCF = !!(inFlags & EFLAGS_MASK_CF);
        const auto outFlags = test.fina.regActive(MOO_RG16_FLAGS) ? test.fina.rg16[MOO_RG16_FLAGS] : test.init.rg16[MOO_RG16_FLAGS];
        const uint8_t outAL = (test.fina.regActive(MOO_RG16_AX) ? test.fina.rg16[MOO_RG16_AX] : test.init.rg16[MOO_RG16_AX]) & 0xff;
        const uint8_t outAF = !!(outFlags & EFLAGS_MASK_AF);
        const uint8_t outCF = !!(outFlags & EFLAGS_MASK_CF);
        const uint8_t outOF = !!(outFlags & EFLAGS_MASK_OF);

        uint8_t expected = 0;
        if (inAL & 0x80) {
            expected = !(outAL & 0x80);
        }
        if (outOF != expected)
            std::println("{:02X} {} {}  --> {:02X} {} {} {}  {:02X}", inAL, inAF, inCF, outAL, outAF, outCF, outOF, inAL ^ outAL);
#endif
        const auto decodedIns = MooDecodeInstruction(test, m8088.cpu().cpuInfo());
        const auto finalState = test.makeFinal();

        const uint8_t width = decodedIns.ins.operandSize;

        const auto m1 = static_cast<uint16_t>(test.init.readReg(REG_AX, width));
        const auto m2 = static_cast<uint16_t>(decodedIns.eaVal[0]);
        const uint32_t actualResult = static_cast<uint32_t>((width > 1 ? finalState.readReg(REG_DX, 2) : 0) << 16 | finalState.readReg(REG_AX, 2));


        int CF = 0;
        int PF = 0;
        auto RRCY = [&CF](uint16_t& reg) {
            const auto outCF = reg & 1;
            reg = static_cast<uint16_t>(CF << 15 | reg >> 1);
            CF = outCF;
        };
        auto ADDF = [&CF,&PF](uint16_t& reg, uint16_t rhs) {
            const auto sum = reg + rhs;
            reg = static_cast<uint16_t>(sum);
            PF = Parity(static_cast<uint8_t>(sum));
            CF = (sum >> 16) & 1;
        };
        auto PASS = [&PF](uint16_t reg) {
            PF = Parity(static_cast<uint8_t>(reg));
        };

        uint16_t tmpC = m1;
        uint16_t tmpA = 0;
        uint16_t tmpB = m2;

        //std::println("{:016b} {:016b} * {:016b}", tmpA, tmpC, tmpB);
        RRCY(tmpC);
        for (int cnt = 0; cnt < width*8; ++cnt) {
            //std::println("{:016b} {:016b} CF={} PF={}", tmpA, tmpC, CF, PF);
            if (CF)
                ADDF(tmpA, tmpB);
            RRCY(tmpA);
            RRCY(tmpC);
        }
        PASS(tmpA);
        //std::println("{:016b} {:016b} PF={}", tmpA, tmpC, PF);

        uint32_t expectedResult = tmpA << 16 | tmpC;
        if (width == 1)
            expectedResult >>= 8;

        if (expectedResult != actualResult) {
            std::println("{} {:4X}*{:4X} = {:8X} computed, actual {:08X}", decodedIns.desc, m1, m2, expectedResult, actualResult);
            exit(1);
        }

        int outPF = !!(finalState.flags() & EFLAGS_MASK_PF);
        if (PF != outPF) {
            std::println("{} {:4X}*{:4X} = {:8X} computed, PF={} expected {}", decodedIns.desc, m1, m2, expectedResult, PF, outPF);
            exit(1);
        }

        return false;
    });
#endif
    if (0) {
        std::println("EARLY EXIT                                                    ");
        exit(0);
    }

    const std::map<std::string, uint32_t> commonIgnoredFlags {
        { "37", EFLAGS_MASK_PF | EFLAGS_MASK_SF | EFLAGS_MASK_OF | EFLAGS_MASK_ZF }, // AAA
        { "3f", EFLAGS_MASK_PF | EFLAGS_MASK_SF | EFLAGS_MASK_OF | EFLAGS_MASK_ZF }, // AAS
        { "d4", EFLAGS_MASK_OF | EFLAGS_MASK_AF | EFLAGS_MASK_CF }, // AAM
        { "d5", EFLAGS_MASK_OF | EFLAGS_MASK_AF | EFLAGS_MASK_CF }, // AAD

        { "f6.6", divUndefinedFlags }, // DIV BYTE
        { "f6.7", divUndefinedFlags }, // IDIV BYTE
        { "f7.6", divUndefinedFlags }, // DIV WORD
        { "f7.7", divUndefinedFlags }, // IDIV WORD
    };

    std::map<std::string, uint32_t> ignoredFlags80386 {
        // AAM - Flags are completely unpredictable on #DE
        { "d4", EFLAGS_MASK_OF | EFLAGS_MASK_AF | EFLAGS_MASK_CF | EFLAGS_MASK_ZF | EFLAGS_MASK_PF },
        // IMUL
        { "0faf", imulUndefinedFlags },
        { "660faf", imulUndefinedFlags },
        { "670faf", imulUndefinedFlags },
        { "67660faf", imulUndefinedFlags },
        // DIV
        { "66f7.6", divUndefinedFlags }, // F7 div
        { "6766f7.6", divUndefinedFlags }, // F7 div
        { "67f6.6", divUndefinedFlags }, // F6 div
        { "67f7.6", divUndefinedFlags }, // F7 div
        // IDIV
        { "66f7.7", divUndefinedFlags }, // F7 idiv
        { "6766f7.7", divUndefinedFlags }, // F7 idiv
        { "67f6.7", divUndefinedFlags }, // F6 idiv
        { "67f7.7", divUndefinedFlags }, // F7 idiv
        { "f6.7", divUndefinedFlags }, // F6 idiv
        // Bit scan
        { "0fbc", bitScanUndefinedFlags }, // 0FBC bsf
        { "0fbd", bitScanUndefinedFlags }, // 0FBD bsr
        { "660fbc", bitScanUndefinedFlags }, // 0FBC bsf
        { "660fbd", bitScanUndefinedFlags }, // 0FBD bsr
        { "670fbc", bitScanUndefinedFlags }, // 0FBC bsf
        { "670fbd", bitScanUndefinedFlags }, // 0FBD bsr
        { "67660fbc", bitScanUndefinedFlags }, // 0FBC bsf
        { "67660fbd", bitScanUndefinedFlags }, // 0FBD bsr
        // Shift/rotate
        { "0fa4", rotUndefinedFlags }, // 0FA4 shld
        { "0fa5", rotUndefinedFlags }, // 0FA5 shld
        { "670fa4", rotUndefinedFlags }, // 0FA4 shld
        { "670fa5", rotUndefinedFlags }, // 0FA5 shld
        { "660fa4", rotUndefinedFlags }, // 0FA4 shld
        { "660fa5", rotUndefinedFlags }, // 0FA5 shld
        { "67660fa4", rotUndefinedFlags }, // 0FA4 shld
        { "67660fa5", rotUndefinedFlags }, // 0FA5 shld
        { "0fac", rotUndefinedFlags }, // 0FAC shrd
        { "0fad", rotUndefinedFlags }, // 0FAD shrd
        { "670fac", rotUndefinedFlags }, // 0FAC shrd
        { "670fad", rotUndefinedFlags }, // 0FAD shrd
        { "660fac", rotUndefinedFlags }, // 0FAC shrd
        { "660fad", rotUndefinedFlags }, // 0FAD shrd
        { "67660fac", rotUndefinedFlags }, // 0FAC shrd
        { "67660fad", rotUndefinedFlags }, // 0FAD shrd
     };

    for (const auto& [key, value] : commonIgnoredFlags) {
        if (ignoredFlags80386.find(key) == ignoredFlags80386.end())
            ignoredFlags80386[key] = value;
    }

    RunTestsInDir(CPUModel::i80386sx, mooTestDir + "386/v1_ex_real_mode/", {}, ignoredFlags80386);
    RunTestsInDir(CPUModel::i8088, mooTestDir + "8088/", {}, commonIgnoredFlags);
}

int main()
{
    try {
        TestMoo();
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
    return 0;
}