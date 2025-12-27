#include <print>
#include <map>
#include <set>
#include <queue>
#include <cstring>
#include "util.h"
#include "fileio.h"
#include "exehdr.h"
#include "decode.h"

namespace {

#include "debug_vxd_info.h"

bool IsRelOp(DecodedEAType t)
{
    switch (t) {
    case DecodedEAType::rel8:
    case DecodedEAType::rel16:
    case DecodedEAType::rel32:
        return true;
    default:
        return false;
    }
}

uint8_t RelOpSize(DecodedEAType t)
{
    switch (t) {
    case DecodedEAType::rel8:
        return 1;
    case DecodedEAType::rel16:
        return 2;
    case DecodedEAType::rel32:
        return 4;
    default:
        throw std::runtime_error{std::format("Not relX! {}", t)};
    }
}

constexpr uint8_t OPSIZE_PMODE_MASK = 0x80;

} // unnamed namespace

class Disassembler {
public:
    explicit Disassembler(CPUModel model, const uint8_t* data, size_t size, std::FILE* fp)
        : model_ { model }
        , data_ { data }
        , size_ { size }
        , fp_ { fp }
    {
    }

    void addRoot(uint32_t offset, std::uint8_t opSize, const char* label = nullptr)
    {
        if (label)
            addLabel(offset, label);
        roots_.push({ offset, opSize });
    }

    void addLabel(uint32_t offset, const std::string& label)
    {
        labels_.insert({ offset, label });
    }

    void analyze();

    uint8_t disasm16(uint32_t offset, uint16_t cs, uint16_t ip)
    {
        const CPUInfo cpuInfo = {
            model_,
            2,
        };
        const auto res = Decode(cpuInfo, [&]() {
            return getU8(offset++);
        });

        const auto addr = Address { cs, ip, cpuInfo.defaultOperandSize };
        std::println(fp_, "{}", FormatDecodedInstructionFull(res, addr));
        return res.numInstructionBytes;
    }

    void print();

    void setRelocBase(uint32_t base)
    {
        relocBase_ = base;
    }

    void addSegmentStart(uint32_t start)
    {
        segmentStarts_.push_back(start);
    }

private:
    const CPUModel model_;
    const uint8_t* data_;
    size_t size_;
    std::FILE* fp_;

    struct AddrInfo {
        uint32_t offset;
        uint8_t opSize;
    };

    struct VisitInfo {
        uint8_t opSize;
        bool root;
    };

    std::map<uint32_t, VisitInfo> visited_;
    std::map<uint32_t, std::string> labels_;
    std::queue<AddrInfo> roots_;
    std::vector<uint32_t> segmentStarts_;
    uint32_t relocBase_ = 0;

    uint8_t getU8(size_t offset) const
    {
        const auto actualOffset = offset - relocBase_;
        if (actualOffset >= size_)
            throw std::runtime_error { std::format("offset {} is out of range during disassembly", offset) };
        return data_[actualOffset];
    }

    uint16_t getU16(size_t offset) const
    {
        return getU8(offset) | getU8(offset + 1) << 8;
    }

    InstructionDecodeResult decode(uint32_t offset, std::uint8_t opSize) const
    {
        const CPUInfo cpuInfo = {
            model_,
            opSize,
        };

        return Decode(cpuInfo, [&]() {
            return getU8(offset++);
        });
    }

    Address fakeAddress(uint32_t offset, uint8_t opSize)
    {
        opSize &= ~OPSIZE_PMODE_MASK;
        if (opSize == 2)
            return Address { static_cast<uint16_t>((offset >> 16) << 12), static_cast<uint16_t>(offset & 0xffff), opSize };
        uint16_t segment = 0;
        for (size_t i = 0; i < segmentStarts_.size(); ++i)
            if (offset >= segmentStarts_[i])
                segment = static_cast<uint16_t>(i + 1);
        return Address { segment, offset, opSize };
    }

    bool handleVisit(uint32_t offset, uint8_t opSize, bool isRoot)
    {
        if (auto it = visited_.find(offset); it != visited_.end()) {
            if (isRoot)
                it->second.root = true;
            return true;
        }
        visited_.insert({ offset, { opSize, isRoot } });
        return false;
    }

    bool isVxDCall(const InstructionDecodeResult& ins, uint8_t opSize)
    {
        return (opSize & OPSIZE_PMODE_MASK) && ins.instruction->mnemonic == InstructionMnem::INT && (ins.ea[0].immediate & 0xff) == 0x20;
    }

    std::string labelName(uint32_t offset)
    {
        return std::format("lab_{:06X}", offset);
    }

    void printLabel(uint32_t offset);
    void printData(uint32_t offset, uint32_t size);
};

void Disassembler::analyze()
{
    while (!roots_.empty()) {
        auto [offset, opSize] = roots_.front();
        roots_.pop();

        bool root = true;

        for (uint32_t nextOffset = offset; offset < relocBase_ + size_; offset = nextOffset) {
            const auto ins = decode(offset, opSize & ~OPSIZE_PMODE_MASK);
            nextOffset = offset + ins.numInstructionBytes;

            if (handleVisit(offset, opSize, root))
                break;
            root = false;

            bool done = false;
            switch (ins.instruction->mnemonic) {
            case InstructionMnem::MOV:
                // Assume CR0 is written to toggle protected mode
                if (ins.ea[0].type == DecodedEAType::creg && ins.ea[0].regNum == 0)
                    opSize ^= OPSIZE_PMODE_MASK;
                break;
            case InstructionMnem::INT:
                // VXDcall
                if (isVxDCall(ins, opSize))
                    nextOffset += 4;
                break;
            case InstructionMnem::JMP:
                done = true;
                [[fallthrough]];
            case InstructionMnem::CALL:
                if (!IsRelOp(ins.ea[0].type))
                    break;
                [[fallthrough]];
            case InstructionMnem::JO:
            case InstructionMnem::JNO:
            case InstructionMnem::JB:
            case InstructionMnem::JNB:
            case InstructionMnem::JZ:
            case InstructionMnem::JNZ:
            case InstructionMnem::JBE:
            case InstructionMnem::JNBE:
            case InstructionMnem::JS:
            case InstructionMnem::JNS:
            case InstructionMnem::JP:
            case InstructionMnem::JNP:
            case InstructionMnem::JL:
            case InstructionMnem::JNL:
            case InstructionMnem::JLE:
            case InstructionMnem::JNLE:
            case InstructionMnem::JCXZ:
            case InstructionMnem::LOOP:
            case InstructionMnem::LOOPZ:
            case InstructionMnem::LOOPNZ:
                // Technically there could be offset wrap around, but only for realmode
                addRoot(static_cast<uint32_t>(nextOffset + SignExtend(ins.ea[0].immediate, RelOpSize(ins.ea[0].type))), opSize);
                break;
            case InstructionMnem::RETN:
            case InstructionMnem::RETF:
            case InstructionMnem::IRET:
                done = true;
                break;
            case InstructionMnem::JMPF:
                done = true;
                [[fallthrough]];
            case InstructionMnem::CALLF: {
                const auto& ea = ins.ea[0];

                switch (ea.type) {
                    // TODO...
                case DecodedEAType::rm16:
                case DecodedEAType::rm32:
                case DecodedEAType::reg16:
                case DecodedEAType::reg32:
                    break;
                case DecodedEAType::abs16_16:
                    if (opSize & OPSIZE_PMODE_MASK) {
                        const auto addr = fakeAddress(offset, opSize);
                        std::println(stderr, "{}: (offset {:X}) Not handled due to protected mode being enabled. {}", addr, offset, FormatDecodedInstruction(ins, addr));
                        //addRoot(addr.segment() * 16 + (ins.ea[0].address & 0xffff), opSize);
                    } else {
                        addRoot(static_cast<uint32_t>((ins.ea[0].address >> 16) * 16 + (ins.ea[0].address & 0xffff)), opSize);
                    }
                    break;
                case DecodedEAType::abs16_32:
                    if (opSize & OPSIZE_PMODE_MASK) {
                        const auto addr = fakeAddress(offset, opSize);
                        std::println(stderr, "{}: (offset {:X}) Not handled due to protected mode being enabled. {}", addr, offset, FormatDecodedInstruction(ins, addr));
                        break;
                    }
                    [[fallthrough]];
                default:
                    throw std::runtime_error { std::format("{} -- TODO {}", FormatDecodedInstructionFull(ins, fakeAddress(offset, opSize)), ea.type) };
                }
                break;
            }
            default:
                ;
            }

            if (done)
                break;
        }
    }
}

void Disassembler::print()
{
    uint32_t lastOffset = 0;

    auto labelLookup = [&](std::uint64_t addrFull) {
        const auto addr = static_cast<uint32_t>(addrFull);
        if (auto it = labels_.find(addr); it != labels_.end())
            return it->second;
        if (auto it = visited_.find(addr); it != visited_.end() && it->second.root)
            return labelName(addr);
        return std::string {};
    };

    for (const auto& [offset, info] : visited_) {
        if (offset > lastOffset)
            printData(lastOffset, offset - lastOffset);

        const auto opSize = static_cast<uint8_t>(info.opSize & ~OPSIZE_PMODE_MASK);

        auto ins = decode(offset, opSize);
        if (info.root)
            printLabel(offset);
        const bool vxdCall = isVxDCall(ins, info.opSize);
        if (vxdCall) {
            for (int i = 0; i < 4; ++i)
                ins.instructionBytes[2 + i] = getU8(offset + 2 + i);
            ins.numInstructionBytes += 4;
        }

        std::print(fp_, "{}", FormatDecodedInstructionFull(ins, fakeAddress(offset, opSize), labelLookup));
        if (vxdCall) {
            const auto vxdId = getU16(offset + 4);
            auto serviceId = getU16(offset + 2);
            std::print(fp_, "\t; VxdCall 0x{:04X},0x{:04X}", vxdId, serviceId);
            serviceId &= 0x7fff;
            if (auto it = vxdNames.find(vxdId); it != vxdNames.end()) {
                std::print(" {}", it->second);
                if (vxdId == 1 && serviceId < std::size(VmmServiceIds)) {
                    std::print(" {}", VmmServiceIds[serviceId]);
                }
            }
        }
        lastOffset = offset + ins.numInstructionBytes;
        std::println(fp_, "");
    }
}

void Disassembler::printLabel(uint32_t offset)
{
    if (auto it = labels_.find(offset); it != labels_.end())
        std::println(fp_, "\t{}:", it->second);
    else
        std::println(fp_, "\t{}:", labelName(offset));
}

void Disassembler::printData(uint32_t offset, uint32_t size)
{
    printLabel(offset);
    constexpr uint32_t bytesPerLine = 8;
    uint32_t lastStart = 0;

    if (size > 0x100) {
        std::println(fp_, "; Skipping 0x{:X} bytes", size);
        return;
    }

    auto finishBlock = [&](uint32_t end) {
        std::print(fp_, "\t; {:06X} '", offset + lastStart);
        for (uint32_t i = lastStart; i < end; ++i) {
            uint8_t ch = data_[offset + i - relocBase_];
            if (ch >= ' ' && ch <= 0x7F)
                std::print(fp_, "{:c}", ch);
            else
                std::print(fp_, "\\x{:02X}", ch);
        }
        std::println(fp_, "'");
    };

    for (uint32_t i = 0; i < size; ++i) {
        if (i % bytesPerLine == 0) {
            lastStart = i;
            std::print(fp_, "\tDB");
        }
        std::print(fp_, "{:c}0x{:02X}", i % bytesPerLine == 0 ? '\t' : ',', data_[offset + i - relocBase_]);
        if ((i + 1) % bytesPerLine == 0)
            finishBlock(i + 1);
    }
    if (size % bytesPerLine)
        finishBlock(size);
}

struct Symbol {
    std::string name;
    std::uint16_t segNum;
    std::uint32_t offset;
};

std::vector<Symbol> ParseSymFile(const std::vector<uint8_t>& data)
{
    if (data.size() < 4)
        throw std::runtime_error { "Invalid SYM file" };

//    const std::string moduleName = std::string(&data[16], &data[16 + data[15]]);
//    std::println("Module name: {:?}", moduleName);

#if 0
    const uint16_t entrySeg = GetU16(&data[4]); // Segment with program entry point
    const uint16_t numInHeader = GetU16(&data[6]); // Number of names before the first segment (0000:xxxx)
    const uint16_t headSize = GetU16(&data[8]); // Size of header (header + segment zero)
    const uint16_t numSeg = GetU16(&data[10]); // Number of segments (no segment zero)
    const uint16_t segOne = GetU16(&data[12]); // Segment one address (old format - in bytes, new format - in paragraphs)

    PR(entrySeg);
    PR(numInHeader);
    PR(headSize);
    PR(numSeg);
    PR(segOne);
#endif
#define PR(x) std::println(stderr, "{:20s} {:0{}x}", #x, x, sizeof(x) * 2)

    uint32_t offset = 0;
    auto get8 = [&]() {
        return data[offset++];
    };
    auto get16 = [&]() {
        const auto val = GetU16(&data[offset]);
        offset += 2;
        return val;
    };
    auto get32 = [&]() {
        const auto val = GetU32(&data[offset]);
        offset += 4;
        return val;
    };
    auto getStr = [&]() {
        uint8_t nameLen = get8();
        std::string name { &data[offset], &data[offset + nameLen] };
        offset += nameLen;
        return name;
    };

    const auto scale = exehdr::ParagraphSize;
    auto getSize = [&]() {
        return get16() * scale;
    };

    const auto symLength = get32() * scale; // Was in bytes in earlier versions
    if (symLength + 4 != data.size())
        throw std::runtime_error { std::format("Invalid SYM file: symLength={} expected={}", symLength, data.size() - 4) };

    [[maybe_unused]] const auto entrySeg = get16();
    const auto symsInSeg0 = get16();
    (void)get16(); // Supposed to be header size
    const auto numSegments = get16(); // supposed to not include segment 0
    const auto firstSegAddr = getSize();
    ++offset;
    const auto moduleName = getStr();

    PR(entrySeg);
    PR(symsInSeg0);
    PR(numSegments);
    PR(firstSegAddr);
    std::println(stderr, "Module name: {:?}", moduleName);
    if (symsInSeg0)
        throw std::runtime_error { std::format("TODO: symsInSeg0={}", symsInSeg0) };
    offset = firstSegAddr;

    std::vector<Symbol> symbols;
    uint32_t numSegs = 1;
    for (; offset + 32 < data.size(); ++numSegs) {
        const auto nextAddr = getSize();
        const auto numSym = get16();
        [[maybe_unused]] const auto symSize = get16();
        const auto segNum = get16();
        offset += 6;
        const auto segType = get8();
        offset += 5;
        auto segName = getStr();
        std::println(stderr, "{:?}", segName);
        PR(nextAddr);
        PR(numSym);
        PR(symSize);
        PR(segNum);
        PR(segType);
        if (segType != 1)
            throw std::runtime_error { std::format("segType {} not supported", segType) };
        for (uint32_t i = 0; i < numSym; ++i) {
            auto addr = get32();
            auto symName = getStr();
            std::println(stderr, "  {:08X} {}", addr, symName);
            symbols.push_back(Symbol { std::move(symName), segNum, addr });
        }
        if (nextAddr < offset)
            break;
        offset = nextAddr;
    }

    if (numSegs != numSegments)
        throw std::runtime_error { std::format("NumSegs = 0x{:X} Expected = 0x{:0X}", numSegs, numSegments) };

#undef PR

    return symbols;
}

[[maybe_unused]] static const void* findByteSequence(const void* hayStack, size_t hayStackSize, const void* needle, size_t needleSize)
{
    if (hayStackSize >= needleSize && needleSize) {
        const uint8_t* start = reinterpret_cast<const uint8_t*>(hayStack);
        const uint8_t* end = start + hayStackSize - needleSize;
        const uint8_t ch = *reinterpret_cast<const uint8_t*>(needle);
        for (const uint8_t* ptr = start; ptr < end; ++ptr) {
            ptr = reinterpret_cast<const uint8_t*>(std::memchr((void*)ptr, ch, end - ptr));
            if (!ptr)
                break;
            if (!memcmp(ptr, needle, needleSize))
                return ptr;
        }
    }

    return nullptr;
}

[[maybe_unused]] static void listAllOccurances(const void* hayStack, size_t hayStackSize, const void* needle, size_t needleSize)
{
    const uint8_t* start = reinterpret_cast<const uint8_t*>(hayStack);
    const uint8_t* end = start + hayStackSize;
    for (auto ptr = start; ptr; ++ptr) {
        ptr = reinterpret_cast<const uint8_t*>(findByteSequence(ptr, end - ptr, needle, needleSize));
        if (!ptr)
            break;
        std::println("{:06X}\n", ptr - start);
    }
}

[[maybe_unused]] static void hexSearch(const void* hayStack, size_t hayStackSize, const char* hexStr)
{
    auto needle = HexDecode(hexStr);
    listAllOccurances(hayStack, hayStackSize, needle.data(), needle.size());
}

[[maybe_unused]] static void foo(const std::vector<uint8_t>& data)
{
    const auto dosHdr = *reinterpret_cast<const exehdr::DosExeHeader*>(data.data());
    const uint32_t hdrSize = dosHdr.cparhdr * exehdr::ParagraphSize;

    Disassembler d { CPUModel::i80386, data.data() + hdrSize, data.size() - hdrSize, stdout };
    d.addRoot(dosHdr.cs * 16 + dosHdr.ip, 2, "Start");

    const auto symFile = ReadFile(R"(c:\prog\xemu\misc\SW\Win16DDK\WIN386.SYM)");
    if (1) {
        HexDump(0, symFile.data(), 256);
        ParseSymFile(symFile);
    }

    exit(0); // TEMP

    // hexSearch(data.data() + hdrSize, data.size() - hdrSize, "9D558BEC9CFCFAAC84C0");
    // exit(0);

#if 0
        d.addRoot(0x18D6, OPSIZE_PMODE_MASK | 2, "Pmode16Start");
        d.addRoot(0x1FA2C, OPSIZE_PMODE_MASK | 4, "Pmode32Start");
        d.addLabel(0x010294, "IntNormalEntry");
        d.addLabel(0x0102A1, "IntCommon");
        d.addLabel(0x01031B, "IntNotV86");

        const uint32_t intHandlerOffsets[] = {
            0x10408, 0x10418, 0x104E0, 0x10428, 0x10438, 0x10448, 0x10458, 0x10468,
            0x10478, 0x10484, 0x10494, 0x104A0, 0x104AC, 0x104B8, 0x104C4, 0x104D0,
            0x10522, 0x1052A, 0x10532, 0x1053A, 0x10542, 0x1054A, 0x10552, 0x1055A,
            0x10562, 0x1056A, 0x10572, 0x1057A, 0x10582, 0x1058A, 0x10592, 0x1059A,
            0x105A2, 0x105AA, 0x105B2, 0x105BA, 0x105C2, 0x105CA, 0x105D2, 0x105DA,
            0x105E2, 0x105EA, 0x105F2, 0x105FA, 0x10602, 0x1060A, 0x10612, 0x1061A,
            0x113AC, 0x1062A, 0x10632, 0x1063A, 0x10642, 0x1064A, 0x10652, 0x1065A,
            0x10662, 0x1066A, 0x10672, 0x1067A, 0x10682, 0x1068A, 0x10692, 0x1069A,
            0x106A2, 0x106AA, 0x106B2, 0x106BA, 0x106C2, 0x106CA, 0x106D2, 0x106DA,
            0x106E2, 0x106EA, 0x106F2, 0x106FA, 0x10702, 0x1070A, 0x10712, 0x1071A,
            0x10722, 0x1072A, 0x10732, 0x1073A, 0x10742, 0x1074A, 0x10752, 0x1075A,
            0x10762, 0x1076A, 0x10772, 0x1077A, 0x10782, 0x1078A, 0x10792, 0x1079A
        };
        for (size_t i = 0; i < std::size(intHandlerOffsets); ++i)
            d.addRoot(intHandlerOffsets[i], OPSIZE_PMODE_MASK | 4, std::format("Int{:02X}Entry", i).c_str());
#else

    d.addRoot(0x19B0, OPSIZE_PMODE_MASK | 2, "Pmode16Start");
    d.addRoot(0x6C584 - hdrSize, OPSIZE_PMODE_MASK | 4, "Pmode32Start");
    d.addRoot(/*0x037f*/ 0x1f75e, OPSIZE_PMODE_MASK | 4, "@D_Out_Debug_String");
#endif
    d.analyze();
    d.print();

    // const auto ofs = 0x1fc2c;
    // HexDump(ofs - hdrSize, data.data() + ofs, 32);

    // uint32_t ofs2 = 0;
    // for (int i = 0; i < 100; ++i) {
    //     ofs2 += d.disasm16(ofs2 + dosHdr.cs * 16 + dosHdr.ip, dosHdr.cs, static_cast<uint16_t>(dosHdr.ip + ofs2));
    // }
}

int main()
{
    try {
        // const auto res = Decode(cpuInfo, fetch);
        //  const char* filename = R"(c:\Misc\TASM1\MAKE.EXE)";
        const char* filename = R"(c:\prog\xemu\misc\SW\Win16DDK\Real\WIN386.386)";
        //const char* filename = R"(c:\prog\xemu\misc\W3Extract\WIN386.386)";
        const auto data = ReadFile(filename);
        if (data.size() < 0x100) {
            std::println(stderr, "File is too small");
            return 1;
        }

        exehdr::PrintExeHeader(stderr, data.data(), data.size());

        const auto dosHdr = *reinterpret_cast<const exehdr::DosExeHeader*>(data.data());
        const uint32_t hdrSize = dosHdr.cparhdr * exehdr::ParagraphSize;
        if (dosHdr.magic != exehdr::IMAGE_DOS_SIGNATURE || sizeof(exehdr::DosExeHeader) + hdrSize >= data.size()) {
            std::println(stderr, "Invalid header");
            return 1;
        }
        const auto lfanew = GetU32(data.data() + exehdr::DosExeHeader_lfanew_offset);
        if (lfanew + sizeof(exehdr::VxDHeader) >= data.size() || GetU32(data.data() + lfanew) != exehdr::IMAGE_VXD_SIGNATURE) {
            std::println(stderr, "Expected VxD file");
            return 1;
        }
        const auto& vxdHdr = *reinterpret_cast<const exehdr::VxDHeader*>(data.data() + lfanew);
        if (vxdHdr.datapage > data.size()) {
            std::println(stderr, "Invalid data page offset");
            return 1;
        }
        const auto objHdr = reinterpret_cast<const exehdr::VxdObjectHeader*>(data.data() + lfanew + vxdHdr.objtab);
        if (vxdHdr.startobj > vxdHdr.objcnt) {
            std::println(stderr, "Bad start object");
            return 1;
        }

        // Assume straight forward page mapping...
        std::vector<uint8_t> relocatedCode { data.begin() + vxdHdr.datapage, data.end() };
        const uint32_t relocBase = 0x80001000;

        const auto fixupPageTable = reinterpret_cast<const uint32_t*>(data.data() + lfanew + vxdHdr.fpagetab);
        for (uint32_t pageNum = 0; pageNum < vxdHdr.mpages; ++pageNum) {
            const auto size = fixupPageTable[pageNum + 1] - fixupPageTable[pageNum];
            if (!size)
                continue;
            //std::println(stderr, "{:2X} {:08X} {:08x}", pageNum, fixupPageTable[pageNum], size);
            const uint8_t* fixupRecord = data.data() + lfanew + vxdHdr.frectab + fixupPageTable[pageNum];
            for (uint32_t ofs = 0; ofs < size;) {
                const auto addressType = fixupRecord[ofs++];
                const auto relocationType = fixupRecord[ofs++];
                //std::println(stderr, "  {:02X} ({:04b}`{:04b}) {:02X} ({:04b}`{:04b})", addressType, addressType >> 4, addressType & 0xf, relocationType, relocationType >> 4, relocationType & 0xf);

                const uint8_t relocAddressType = addressType & 0xf;
                enum {
                    REL_TYPE_OFS32 = 7, // 32-bits Offset
                    REL_TYPE_REL32 = 8, // 32-bits Offset Relatively EIP
                };

                constexpr uint8_t RELOC_TYPE_MASK_TARGET32 = 0x10;

                if ((relocAddressType != REL_TYPE_OFS32 && relocAddressType != REL_TYPE_REL32) || (addressType & 0x10)) { // 0x10 = Fixup to 16:16 alias
                    std::println(stderr, "Unsupported addressType {:02X} ({:04b}`{:04b})", addressType, addressType >> 4, addressType & 0xf);
                    goto fail;
                }

                if (relocationType & ~RELOC_TYPE_MASK_TARGET32) { // low bits: 0b00 = Internal reference
                    std::println(stderr, "Unsupported relocationType {:02X} ({:04b}`{:04b})", relocationType, relocationType >> 4, relocationType & 0xf);
                fail:
                    HexDump(0, fixupRecord + ofs, 16);
                    exit(1);
                }


                struct FixupTarget {
                    uint16_t object;
                    uint32_t offset;
                } target;
                auto getTargetData = [&]() {
                    const uint16_t object = fixupRecord[ofs++];
                    uint32_t offset;
                    if (relocationType & RELOC_TYPE_MASK_TARGET32) {
                        offset = GetU32(&fixupRecord[ofs]);
                        ofs += 4;
                    } else {
                        offset = GetU16(&fixupRecord[ofs]);
                        ofs += 2;
                    }
                    return FixupTarget { object, offset };
                };

                auto applyFixup = [&](uint16_t sourceOffset) {
                    if (relocAddressType == REL_TYPE_REL32) // EIP relative points don't need fixup
                        return;
                    uint8_t* data = &relocatedCode[pageNum * vxdHdr.pagesize + sourceOffset];
                    PutU32(data, GetU32(data) + relocBase);
                };

                if (addressType & 0x20) {
                    const uint8_t numSrcOffs = fixupRecord[ofs++];
                    target = getTargetData();
                    //std::println(stderr, "    {:02X}:{:08X}", target.object, target.offset);
                    for (uint32_t j = 0; j < numSrcOffs; ++j) {
                        const uint16_t relOfs = GetU16(&fixupRecord[ofs]);
                        ofs += 2;
                        // std::println(stderr, "      {:04X}", relOfs);
                        applyFixup(relOfs);
                    }
                } else {
                    const uint16_t relOfs = GetU16(&fixupRecord[ofs]);
                    ofs += 2;
                    target = getTargetData();
                    applyFixup(relOfs);
                    //std::println(stderr, "    {:04X} -> {:02X}:{:08X}", relOfs, target.object, target.offset);
                }
            }
        }

        //HexDump(vxdHdr.fpagetab, data.data() + lfanew + vxdHdr.fpagetab, vxdHdr.frectab -  vxdHdr.fpagetab);

        const auto symFile = ReadFile(R"(c:\prog\xemu\misc\SW\Win16DDK\WIN386.SYM)");
        //HexDump(0, symFile.data(), 256);
        const auto symbols = ParseSymFile(symFile);

        // N.B. segment 2 is actually loaded to 80283D84
        Disassembler d { CPUModel::i80386, relocatedCode.data(), relocatedCode.size(), stdout };
        d.setRelocBase(relocBase);
        for (uint32_t i = 0; i < vxdHdr.objcnt; ++i)
            d.addSegmentStart(objHdr[i].relocationBaseAddress + relocBase);       

        d.addRoot(objHdr[vxdHdr.startobj - 1].relocationBaseAddress + relocBase + vxdHdr.eip, OPSIZE_PMODE_MASK | 4, "Entry");
        #if 1
        const struct {
            uint8_t intNo;
            uint32_t offset;
        } intEntries[] = {
            { 0x00, 0x80006E08 },
            { 0x01, 0x80006E18 },
            { 0x02, 0x80006EE0 },
            { 0x03, 0x80006E28 },
            { 0x04, 0x80006E38 },
            { 0x05, 0x80006E48 },
            { 0x06, 0x80006E58 },
            { 0x07, 0x80006E68 },
            { 0x08, 0x80006E78 },
            { 0x09, 0x80006E84 },
            { 0x0A, 0x80006E94 },
            { 0x0B, 0x80006EA0 },
            { 0x0C, 0x80006EAC },
            { 0x0D, 0x80006EB8 },
            { 0x0E, 0x80006EC4 },
            { 0x0F, 0x80006ED0 },
            { 0x11, 0x80006F2A },
            { 0x12, 0x80006F32 },
            { 0x14, 0x80006F42 },
            { 0x16, 0x80006F52 },
            { 0x17, 0x80006F5A },
            { 0x18, 0x80006F62 },
            { 0x19, 0x80006F6A },
            { 0x1A, 0x80006F72 },
            { 0x1B, 0x80006F7A },
            { 0x1D, 0x80006F8A },
            { 0x1F, 0x80006F9A },
            { 0x20, 0x80006FA2 },
            { 0x22, 0x80006FB2 },
            { 0x23, 0x80006FBA },
            { 0x27, 0x80006FDA },
            { 0x28, 0x80006FE2 },
            { 0x29, 0x80006FEA },
            { 0x2A, 0x80006FF2 },
            { 0x2B, 0x80006FFA },
            { 0x2C, 0x80007002 },
            { 0x2D, 0x8000700A },
            { 0x2E, 0x80007012 },
            { 0x32, 0x80007032 },
            { 0x34, 0x80007042 },
            { 0x35, 0x8000704A },
            { 0x36, 0x80007052 },
            { 0x37, 0x8000705A },
            { 0x38, 0x80007062 },
            { 0x39, 0x8000706A },
            { 0x3A, 0x80007072 },
            { 0x3B, 0x8000707A },
            { 0x3C, 0x80007082 },
            { 0x3D, 0x8000708A },
            { 0x40, 0x800070A2 },
            { 0x41, 0x800070AA },
            { 0x43, 0x800070BA },
            { 0x44, 0x800070C2 },
            { 0x45, 0x800070CA },
            { 0x46, 0x800070D2 },
            { 0x47, 0x800070DA },
            { 0x48, 0x800070E2 },
            { 0x49, 0x800070EA },
            { 0x4A, 0x800070F2 },
            { 0x4B, 0x800070FA },
            { 0x4C, 0x80007102 },
            { 0x4D, 0x8000710A },
            { 0x4E, 0x80007112 },
            { 0x4F, 0x8000711A },
            { 0x50, 0x80007122 },
            { 0x51, 0x8000712A },
            { 0x52, 0x80007132 },
            { 0x53, 0x8000713A },
            { 0x54, 0x80007142 },
            { 0x55, 0x8000714A },
            { 0x56, 0x80007152 },
            { 0x57, 0x8000715A },
            { 0x58, 0x80007162 },
            { 0x59, 0x8000716A },
            { 0x5A, 0x80007172 },
            { 0x5B, 0x8000717A },
            { 0x5C, 0x80007182 },
            { 0x5D, 0x8000718A },
            { 0x5E, 0x80007192 },
            { 0x5F, 0x8000719A },
        };

        for (const auto& [intNo, offset] : intEntries)
            d.addRoot(offset, OPSIZE_PMODE_MASK | 4, std::format("Int{:02X}Entry", intNo).c_str());

        d.addLabel(0x80006CA1, "IntCommonEntry");

        //for (size_t i = 0; i < 0x100; ++i) // 0x80011108 / 0x80010F88
        //    std::println("{:02X} {:08X}", i, GetU32(relocatedCode.data() + 0x80010F88 - relocBase + i * 4));
        //exit(0);
        d.addRoot(0x8000719F, OPSIZE_PMODE_MASK | 4, "NMIHandler");
        d.addRoot(0x800071E6, OPSIZE_PMODE_MASK | 4, "MaybeIntHandler01");
        d.addRoot(0x8000725A, OPSIZE_PMODE_MASK | 4, "GenericIntHandler");
        d.addRoot(0x80007285, OPSIZE_PMODE_MASK | 4, "DebugIntHandler"); // Int 01h/03h/22h
        d.addRoot(0x80007393, OPSIZE_PMODE_MASK | 4, "Int20Handler"); // Int 20h
        d.addRoot(0x8000BFCD, OPSIZE_PMODE_MASK | 4, "UnexpectedInterrupt"); // Only when V86=0

        #else
        d.addRoot(0x8000F15E, OPSIZE_PMODE_MASK | 4, "@D_Out_Debug_String");
        d.addRoot(0x8000F360, OPSIZE_PMODE_MASK | 4, "@D_Out_Debug_Chr");
        const struct {
            uint8_t intNo;
            uint32_t offset;
        } intEntries[] = {
            { 0x00, 0x8001196C },
            { 0x01, 0x8001197C },
            { 0x02, 0x80011B40 },
            { 0x03, 0x8001198C },
            { 0x04, 0x8001199C },
            { 0x05, 0x800119AC },
            { 0x06, 0x800119BC },
            { 0x07, 0x800119CC },
            { 0x08, 0x800119DC },
            { 0x09, 0x800119E8 },
            { 0x0A, 0x800119F8 },
            { 0x0B, 0x80011A04 },
            { 0x0C, 0x80011A10 },
            { 0x0D, 0x80011A1C },
            { 0x0E, 0x80011A28 },
            { 0x0F, 0x80011B30 },
            { 0x11, 0x80011B8A },
            { 0x12, 0x80011B92 },
            { 0x14, 0x80011BA2 },
            { 0x16, 0x80011BB2 },
            { 0x17, 0x80011BBA },
            { 0x18, 0x80011BC2 },
            { 0x19, 0x80011BCA },
            { 0x1A, 0x80011BD2 },
            { 0x1B, 0x80011BDA },
            { 0x1D, 0x80011BEA },
            { 0x1F, 0x80011BFA },
            { 0x20, 0x80011C02 },
            { 0x22, 0x80011C12 },
            { 0x23, 0x80011C1A },
            { 0x27, 0x80011C3A },
            { 0x28, 0x80011C42 },
            { 0x29, 0x80011C4A },
            { 0x2A, 0x80011C52 },
            { 0x2B, 0x80011C5A },
            { 0x2C, 0x80011C62 },
            { 0x2D, 0x80011C6A },
            { 0x2E, 0x80011C72 },
            { 0x32, 0x80011C92 },
            { 0x34, 0x80011CA2 },
            { 0x35, 0x80011CAA },
            { 0x36, 0x80011CB2 },
            { 0x37, 0x80011CBA },
            { 0x38, 0x80011CC2 },
            { 0x39, 0x80011CCA },
            { 0x3A, 0x80011CD2 },
            { 0x3B, 0x80011CDA },
            { 0x3C, 0x80011CE2 },
            { 0x3D, 0x80011CEA },
            { 0x40, 0x80011D02 },
            { 0x41, 0x80011D0A },
            { 0x43, 0x80011D1A },
            { 0x44, 0x80011D22 },
            { 0x45, 0x80011D2A },
            { 0x46, 0x80011D32 },
            { 0x47, 0x80011D3A },
            { 0x48, 0x80011D42 },
            { 0x49, 0x80011D4A },
            { 0x4A, 0x80011D52 },
            { 0x4B, 0x80011D5A },
            { 0x4C, 0x80011D62 },
            { 0x4D, 0x80011D6A },
            { 0x4E, 0x80011D72 },
            { 0x4F, 0x80011D7A },
            { 0x50, 0x80011D82 },
            { 0x51, 0x80011D8A },
            { 0x52, 0x80011D92 },
            { 0x53, 0x80011D9A },
            { 0x54, 0x80011DA2 },
            { 0x55, 0x80011DAA },
            { 0x56, 0x80011DB2 },
            { 0x57, 0x80011DBA },
            { 0x58, 0x80011DC2 },
            { 0x59, 0x80011DCA },
            { 0x5A, 0x80011DD2 },
            { 0x5B, 0x80011DDA },
            { 0x5C, 0x80011DE2 },
            { 0x5D, 0x80011DEA },
            { 0x5E, 0x80011DF2 },
            { 0x5F, 0x80011DFA },
        };

        for (const auto& [intNo, offset] : intEntries)
            d.addRoot(offset, OPSIZE_PMODE_MASK | 4, std::format("Int{:02X}Entry", intNo).c_str());

        d.addLabel(0x800114C0, "ServiceEntry");
        d.addLabel(0x800114CD, "IntCommonEntry");
        d.addLabel(0x80011718, "Int_NotV86");
        #endif

        for (const auto& sym : symbols) {
            if (sym.segNum != 1)
                continue;
            if (sym.segNum > vxdHdr.objcnt) {
                std::println(stderr, "Bad symbol");
                return 1;
            }
            // Why this offset?
            //d.addRoot(sym.offset + 0xdddf, OPSIZE_PMODE_MASK | 4, sym.name.c_str());
            //std::println(stderr, "{:08X} {}", sym.offset, sym.name);
        }

        d.analyze();
        d.print();

    } catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return 1;
    }
}
