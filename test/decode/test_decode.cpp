#include "cpu.h"
#include "util.h"
#include <print>

struct DecodeTestCase {
    const char* bytesHex;
    const char* expected;
    std::uint32_t address = 0x1000;
};

void RunTests(const CPUInfo& cpuInfo, const DecodeTestCase* tests, size_t numTests)
{
    for (size_t i = 0; i < numTests; ++i) {
        const auto& tc = tests[i];
        try {
            const auto bytes = HexDecode(tc.bytesHex);
            size_t offset = 0;
            auto fetch = [&]() {
                if (offset == bytes.size())
                    throw std::runtime_error { "Too many bytes read" };
                return bytes[offset++];
            };
            const auto res = Decode(cpuInfo, fetch);

            if (res.numInstructionBytes != bytes.size() && !(res.numInstructionBytes == MaxInstructionBytes && bytes.size() > res.numInstructionBytes))
                throw std::runtime_error { "Only " + std::to_string(res.numInstructionBytes) + " / " + std::to_string(bytes.size()) + " bytes consumed" };

            const auto addr = Address { static_cast<uint16_t>(tc.address >> 16), tc.address & 0xffff, cpuInfo.defaultOperandSize };
            const auto str = FormatDecodedInstruction(res, addr);

            if (str != tc.expected) {
                throw std::runtime_error { "Expected " + std::format("\n{:?}", tc.expected) + " got\n" + std::format("{:?}", str) };
            }
        } catch (const std::exception& e) {
            throw std::runtime_error { "Test failed for " + std::string { tc.bytesHex } + ": " + e.what() };
        }
    }
}

template <size_t NumTests>
void RunTests(const CPUInfo& cpuInfo, const DecodeTestCase (&tests)[NumTests])
{
    RunTests(cpuInfo, tests, NumTests);
}

void TestDecode16(CPUModel model)
{
    const CPUInfo cpuInfo = {
        model,
        2,
    };

    constexpr const DecodeTestCase basic[] = {
        { "B84000", "MOV\tAX, 0x0040" },
        { "BB5555", "MOV\tBX, 0x5555" },
        { "CD21", "INT\t0x21" },
        { "CC", "INT3" },
        { "EE", "OUT\tDX, AL" },
        { "26C706140054FF", "MOV\tWORD [ES:0x0014], 0xFF54" },
        { "83C202", "ADD\tDX, 0x02" },
        { "7406", "JZ\t0x02E3", 0x2DB },
        { "26FF1E6700", "CALLF\t[ES:0x0067]" },
        { "204269", "AND\t[BP+SI+0x69], AL" },
        { "E80915", "CALL\t0x19EE", 0x4E2 },
        { "2E8B14", "MOV\tDX, [CS:SI]" },
        { "F3AA", "REP STOSB" },
        { "F3AB", "REP STOSW" },
        { "C3", "RETN" },
        { "90", "NOP" },
        { "26C51D", "LDS\tBX, [ES:DI]" },
        { "87D1", "XCHG\tDX, CX" },
        { "CF", "IRET" },
        { "F6A4003F", "MUL\tBYTE [SI+0x3F00]" },
        { "2EF6FD", "CS IDIV\tCH" },
    };

    RunTests(cpuInfo, basic);

    if (model < CPUModel::i80386sx) {
        // Only the two lower bits are used..
        constexpr const DecodeTestCase t8086[] = {
            { "268CB43D01", "MOV\t[ES:SI+0x013D], SS" },
        };
        RunTests(cpuInfo, t8086);
        return;
    }

    //
    // 386+
    //
    constexpr const DecodeTestCase t386[] = {
        { "0F03D2", "LSL\tDX, DX" }, // 286+
        { "660F024606", "LAR\tEAX, [BP+0x06]" }, // 286+
        { "0F01E0", "SMSW\tAX" }, // 286+
        { "0F00C8", "STR\tAX" }, // 286+
        { "8ED8", "MOV\tDS, AX" },
        { "6631C0", "XOR\tEAX, EAX" },
        { "67C70485000000008BD5", "MOV\tWORD [EAX*4+0x00000000], 0xD58B" },
        { "66B900000200", "MOV\tECX, 0x00020000" },
        { "66F7E8", "IMUL\tEAX" },
        { "26678803", "MOV\t[ES:EBX], AL" },
        { "669AE513000000F0", "CALLF\t0xF000:0x000013E5" },
        { "260FB21D", "LSS\tBX, [ES:DI]" },
        { "8CE8", "MOV\tAX, GS" },
        { "F3AB", "REP STOSW" },
        { "F366AB", "REP STOSD" },
        { "66E806000000", "CALL\t0x0000138D", 0x1381 },
        { "67897302", "MOV\t[EBX+0x02], SI" },
        { "60", "PUSHA" },
        { "6660", "PUSHAD" },
        { "61", "POPA" },
        { "6661", "POPAD" },
        { "2E660F011ED31B", "LIDT\t[CS:0x1BD3]" }, // o32 lidt [cs:0x1bd3]
        { "6667399C4D00400000", "CMP\t[EBP+ECX*2+0x00004000], EBX" }, // cmp[ebp + ecx * 2 + 0x4000], ebx
        { "0F22DE", "MOV\tCR3, ESI" },
        { "0F20C0", "MOV\tEAX, CR0" },
        { "EA421D1000", "JMPF\t0x0010:0x1D42" },
        { "9C", "PUSHF" },
        { "669C", "PUSHFD" },
        { "9D", "POPF" },
        { "669D", "POPFD" },
        { "66CF", "IRETD" },
        { "0FB5DA", "LGS\tBX, DX" }, // Invalid opcode, but allow decoding
        { "66676B24E5750500002D", "IMUL\tESP, [0x00000575], 0x2D" },
        { "67668CC3", "MOV\tEBX, ES" }, // N.B. unsused address-size prefix
        { "67668C6199", "MOV\t[ECX-0x67], FS" }, // N.B. unused operand-size prefix
        { "66666666666666666666666666666690", "UNDEF" }, // Too long
    };

    RunTests(cpuInfo, t386);
}

void TestDecode32(CPUModel model)
{
    const CPUInfo cpuInfo = {
        model,
        4,
    };

    constexpr const DecodeTestCase t386[] = {
        { "2EC51DAF1B0000", "LDS\tEBX, [CS:0x00001BAF]" },
        { "8D6C24FC", "LEA\tEBP, [ESP-0x04]" },
        { "6466893B", "MOV\t[FS:EBX], DI" },
        { "2E0FBE05A7D50000", "MOVSX\tEAX, BYTE [CS:0x0000D5A7]" },
        { "C74500EFBEADDE", "MOV\tDWORD [EBP+0x00], 0xDEADBEEF" },
        { "A231000000", "MOV\t[0x00000031], AL" },
        { "882532000000", "MOV\t[0x00000032], AH" },
        { "D1E9", "SHR\tECX, 0x01" },
        { "F0A300000000", "LOCK MOV\t[0x00000000], EAX" },
        { "63D8", "ARPL\tAX, BX" },
        { "66621D00000200", "BOUND\tBX, [0x00020000]" },
        { "66C8010000", "ENTER\t0x0001, 0x00" },
        { "0F00CB", "STR\tEBX" },
        { "36FF8074440580", "INC\tDWORD [SS:EAX-0x7FFABB8C]" },
        { "8322FE", "AND\tDWORD [EDX], 0xFFFFFFFE" },
        { "FF96080E0180", "CALL\tDWORD [ESI-0x7FFEF1F8]" },
    };

    RunTests(cpuInfo, t386);
}

int main()
{
    try {
        //constexpr const DecodeTestCase tests[] = {
        //    { "FF96080E0180", "CALL\tDWORD [ESI+0x8001E080]" },
        //};
        //RunTests(CPUInfo { CPUModel::i80386, 4 }, tests);

        TestDecode16(CPUModel::i8088);
        TestDecode16(CPUModel::i8086);
        TestDecode16(CPUModel::i80386sx);
        TestDecode32(CPUModel::i80386sx);
    } catch (const std::exception& e){
        std::println("{}", e.what());
        return 1;
    }
    return 0;
}
