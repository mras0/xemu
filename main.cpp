#include <print>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <cstring>
#include "address.h"
#include "fileio.h"
#include "util.h"
#include "cpu.h"
#include "system_bus.h"
#include "debugger.h"
#include "gui.h"
#include "devs/cga.h"
#include "devs/i8259a_pic.h"
#include "devs/i8253_pit.h"
#include "devs/nec765_floppy_controller.h"
#include "devs/i8237a_dma_controller.h"

//////////////////////////////////////////////////////////////////////////////////// 
// Decode tests
////////////////////////////////////////////////////////////////////////////////////

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

template<size_t NumTests>
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

    if (model < CPUModel::i80386) {
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
    };

    RunTests(cpuInfo, t386);
}

class XTPPI : public IOHandler, public CycleObserver {
public:
    // https://github.com/tmk/tmk_keyboard/wiki/IBM-PC-XT-Keyboard-Protocol
    using IRQHandler = std::function<void (bool irqState)>;

    explicit XTPPI(SystemBus& bus, const IRQHandler& irqHandler)
        : bus_ { bus }
        , irqHandler_ { irqHandler }
    {
        bus.addIOHandler(0x60, 4, *this, true);
        bus.addCycleObserver(*this);
        reset();
    }

    void reset()
    {
        portB_ = 0;
        hasScancode_ = false;
        scancode_ = 0;
        resetCnt_ = 0;
        keyboardBuffer_.clear();
    }

    void runCycles([[maybe_unused]] std::uint64_t numCycles) override
    {
        if (resetCnt_) {
            if (numCycles > resetCnt_)
                resetCnt_ = 0;
            else
                resetCnt_ -= static_cast<uint32_t>(numCycles);
            if (!resetCnt_) {
                std::println("XT keyboard - sending handhake");
                setScancode(0xAA);
                return;
            }
        }
        if (canBufferKey()) {
            setScancode(keyboardBuffer_.front());
            keyboardBuffer_.erase(keyboardBuffer_.begin());
        }
    }

    std::uint64_t nextAction() override
    {
        if (resetCnt_)
            return resetCnt_;
        return canBufferKey() ? 1 : UINT64_MAX;
    }

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override
    {
        switch (offset) {
        case 0: // port A
            if (!hasScancode_) {
                std::println("XT keyboard: Read wihout data");
                return scancode_ == 0xAA ? 0 : scancode_; // Return last..
            }
            hasScancode_ = false;
            std::println("XT keyboard: Read scancode: {:02X}", scancode_);
            return scancode_;
        case 1: // port B
            return portB_;
        case 2: // port C
            // 0x62 XT equipment switches (port C)
            return 0b00001101 | 0b10 << 4; // bit 5/4 = 0b10 -> initial video 80*25 color (mono mode)
        default:
            return IOHandler::inU8(port, offset);
        }
    }

    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override
    {
        switch (offset) {
        case 0: // Port A
            // Used during POST
            std::println("XT PPI: Port A output: {:02X}", value);
            break;
        case 1: // Port B
            // Bitfields for Progr. Peripheral Interface (8255) system control port [output]:
            // Bit(s)	Description	(Table P0394)
            //  7	clear keyboard (only pulse, normally kept at 0)
            //  6	=0  hold keyboard clock low
            //  5	NMI I/O parity check disable
            //  4	NMI RAM parity check disable
            //  3	=0 read low nybble of switches S2
            // 	=1 read high nybble of switches S2
            //  2	reserved, often used as turbo switch
            // 	original PC: cassette motor off
            //  1	speaker data enable
            //  0	timer 2 gate to speaker enable
            // Note:	bits 2 and 3 are sometimes used as turbo switch
            std::println("XT PPI: Port B write {:02X} 0b{:08b}", value, value);
            if (value & 0x80) {
                std::println("XT keyboard clear");
                hasScancode_ = false;
                irqHandler_(false);
            }
            if ((value & 0x40) && !(portB_ & 0x40)) {
                std::println("XT keyboard reset");
                //setScancode(0xAA);
                resetCnt_ = 300; // Simulate time for handshake to clock out (for IBM PC XT BIOS, KBD_RESET with I=1)                
            }
            portB_ = value;
            break;
        case 3:
            std::println("XT PPI: Control={:02X} 0b{:08b}", value, value);
            // 89: A/B output, C input (port A used as output during POST by IBM XT BIOS)
            // 99: A=mode 0/input, B=mode0/output C=input
            if (value != 0x89 && value != 0x99)
                throw std::runtime_error { std::format("XT PPI: Unsupported write to control register 0x{:02X}", value) };
            break;
        default:
            IOHandler::outU8(port, offset, value);
        }
    }

    void enqueueScancode(uint8_t scancode) {
        keyboardBuffer_.push_back(scancode);
        bus_.recalcNextAction();
    }

private:
    SystemBus& bus_;
    IRQHandler irqHandler_;
    uint8_t portB_;
    bool hasScancode_;
    uint8_t scancode_;
    uint32_t resetCnt_;
    std::vector<uint8_t> keyboardBuffer_;

    bool canBufferKey() const
    {
        return !hasScancode_ && keyboardEnabled() && !keyboardBuffer_.empty();
    }

    bool keyboardEnabled() const
    {
        return (portB_ & 0xC0) == 0x40;
    }

    void setScancode(uint8_t scancode)
    {
        //if (!keyboardEnabled())
        //    return;

        if (hasScancode_) {
            std::println("XT keyboard overrun");
            scancode_ = 0xFF;
        } else {
            hasScancode_ = true;
            scancode_ = scancode;
        }
        irqHandler_(true);
    }
};

class BaseMachine {
public:
    explicit BaseMachine(CPUModel model, uint32_t baseMemSize = 640*1024)
        : bus { }
        , cpu { model, bus }
        , conventionalMem { baseMemSize }
    {
        bus.addMemHandler(0, conventionalMem.size(), conventionalMem);
    }

    SystemBus bus;
    CPU cpu;
    RamHandler conventionalMem;

    virtual void keyboardEvent(const decltype(GUI::Event::key)& key)
    {
        std::println("Ignoring key scanCode=0x{:02X} down={}", key.scanCode, key.down);
    }
};


class Test386Machine : public BaseMachine, public IOHandler {
public:
    explicit Test386Machine(const char* romFileName)
        : BaseMachine { CPUModel::i80586 } // Pretends to be 386 but tests undocumented ss > 0
        , rom_ { ReadFile(romFileName) }
        , debugFile_ { "out.txt", std::ofstream::binary }
    {
        bus.addIOHandler(debugPort, 1, *this);
        bus.addIOHandler(postPort, 1, *this);
        bus.addMemHandler(1024 * 1024 - rom_.size(), rom_.size(), rom_);
    }

    void outU8(uint16_t port, uint16_t, std::uint8_t value) override
    {
        switch (port) {
        case debugPort:
            debugBuffer_ += value;
            if (value == '\n') {
                debugFile_.write(debugBuffer_.c_str(), debugBuffer_.length());
                debugBuffer_.clear();
            }
            break;
        case postPort:
            std::println("POST: 0x{:02X}", value);
            if (value == 0xff) {
                std::println("Success!");
                debugFile_.close();
#ifdef WIN32
                exit(system("comp /M /L out.txt \"../misc/test386.asm/test386-EE-reference.txt\""));
#else
                exit(system("diff out.txt \"../misc/test386.asm/test386-EE-reference.txt\""));
#endif
            }
            break;
        default:
            IOHandler::outU8(port, port, value);
        }
    }
private:
    static constexpr uint16_t debugPort = 0xe9;
    static constexpr uint16_t postPort = 0x190;

    RomHandler rom_;
    std::string debugBuffer_;
    std::ofstream debugFile_;
};

class XTMachine : public BaseMachine, public IOHandler {
public:
    explicit XTMachine()
        : BaseMachine { CPUModel::i8088 }
        , pic { bus, 0x20 }
        , pit { bus,
            [this]() {
                // std::println("PIT interrupt");
                pic.setInterrupt(PIC_IRQ_PIT);
            } }
        , dma { bus, 0x00, 0x81 }
        , ppi { bus,
            [this](bool state) {
                std::println("XT Keyboard interrupt state {}", state);
                if (state)
                    pic.setInterrupt(PIC_IRQ_KEYBOARD);
                else
                    pic.clearInterrupt(PIC_IRQ_KEYBOARD);
            } }
        , floppy {
            bus,
            [this]() { pic.setInterrupt(PIC_IRQ_FLOPPY); },
            [this](bool isPut, DMAHandler& handler) {
                assert(!isPut);
                (void)isPut;
                dma.startGet(DMA_CHANNEL_FLOPPY, handler);
            },
        }
        , cga { bus }
    {
        bus.setDefaultIOHandler(this);
        cpu.setInterruptFunction([this]() { return pic.getInterrupt(); });
    }
    i8259a_PIC pic;
    i8253_PIT pit;
    i8237a_DMAController dma;
    XTPPI ppi;
    NEC765_FloppyController floppy;
    CGA cga;

    std::uint8_t inU8(uint16_t port, uint16_t) override
    {
        bool log = true;
        if (port == 0x201) {
            // game port (polled 100 times)
            static bool warned = false;
            if (!warned)
                warned = true;
            else
                log = false;
        } else if (port == 0x210) {
            // expansion I/O
        } else if (isCommPort(port)) {
        } else if (port == 0x2c1 || port == 0x241 || port == 0x341) {
            // RTC ports
        } else {
            return IOHandler::inU8(port, port);
        }
        if (log)
            std::println("{} TODO: IN8 0x{:04X}", cpu.currentIp(), port);
        return 0xFF;
    }

    void outU8(uint16_t port, uint16_t, std::uint8_t value) override
    {
        if (port == 0xA0) {
            // XT: NMI enable (bit 7)
        } else if (port == 0xC0) {
            // Used as dummy port by XT bios
        } else if (port == 0x210 || port == 0x213) {
            // XT Expansion unit enable
        } else if (port >= 0x3B0 && port <= 0x3BA) {
            // MDA
        } else if (isCommPort(port)) {
        } else {
            IOHandler::outU8(port, port, value);
        }
        std::println("{} TODO: OUT 0x{:04X} 0x{:02X}", cpu.currentIp(), port, value);
    }

    void keyboardEvent(const decltype(GUI::Event::key)& key) override
    {
        ppi.enqueueScancode(static_cast<uint8_t>(key.scanCode | (key.down ? 0 : 0x80)));
    }

private:
    static constexpr bool isCommPort(uint16_t port)
    {
        if (port == 0x3FA || port == 0x2FA || port == 0x3EA || port == 0x2EA || port == 0x3BE || port == 0x37A || port == 0x27A)
            return true;
        return port == 0x3BC || port == 0x378 || port == 0x278 || port == 0x3FB || port == 0x2FB; // LPT1-3 / COM1-2
    }
};

class i8042_KeyboardController : public IOHandler {
public:
    explicit i8042_KeyboardController(SystemBus& bus)
    {
        bus.addIOHandler(0x60, 5, *this, true);
    }


    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override
    {
        switch (offset) {
        case 4:
            std::println("i8042: TODO returning 0 for read from port {:02X}", port);
            return 0;
        default:
            std::println("i8042: TODO!");
            return IOHandler::inU8(port, offset);
        }
    }

    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override
    {
        std::println("i8042: TODO!");
        IOHandler::outU8(port, offset, value);
    }
};

class CMOS : public IOHandler {
public:
    explicit CMOS(SystemBus& bus)
        : data_(128)
    {
        bus.addIOHandler(0x70, 2, *this);
        reset();
    }

    void reset()
    {
        reg_ = 0;
    }

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override
    {
        switch (offset) {
        case 1:
            std::println("CMOS: Read from reg {:02X}", reg_ & indexMask);
            return data_[reg_ & indexMask];
        default:
            std::println("CMOS: TODO!");
            return IOHandler::inU8(port, offset);
        }
    }

    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override
    {
        switch (offset) {
        case 0:
            // N.B. bit7 = NMI disable
            reg_ = value;
            break;
        case 1:
            std::println("TODO: CMOS write offset 0x{:02X} value {:02X}", reg_ & indexMask, value);
            data_[reg_ & indexMask] = value;
            break;
        default:
            IOHandler::outU8(port, offset, value);
        }
    }

private:
    static constexpr uint8_t indexMask = 127;
    uint8_t reg_;
    std::vector<uint8_t> data_;
};


class Clone386Machine : public BaseMachine, public IOHandler {
public:
    explicit Clone386Machine()
        : BaseMachine { CPUModel::i80386 }
        , kbd { bus }
        , cmos { bus }
        , dma1 { bus, 0x00, 0x81 }
        , dma2 { bus, 0xC0, 0x89 }
    {
        bus.setDefaultIOHandler(this);
    }

    i8042_KeyboardController kbd;
    CMOS cmos;
    i8237a_DMAController dma1;
    i8237a_DMAController dma2;


    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override
    {
        switch (port) {
        case 0x80:
        case 0xE1:
        case 0xE2:
        case 0x8022: // XXX???
            std::println("Ignoring write to port {:02X} value {:02X}", port, value);
            break;
        default:
            IOHandler::outU8(port, offset, value);
        }
    }
};

void StretchImage(uint32_t* dst, int dstW, int dstH, const uint32_t* src, int srcW, int srcH)
{
    assert(srcW <= dstW && srcH <= dstH);

    if (!srcW || !srcH) {
        std::memset(dst, 0, dstW * dstH * sizeof(*dst));
        return;
    }

    const auto xScale = (double)dstW / srcW;
    const auto yScale = (double)dstH / srcH;
    if ((xScale != 1 && xScale != 2) || yScale != 2)
        throw std::runtime_error { std::format("TODO: StretchImage scale = {}x{}", (double)dstW / srcW, (double)dstH / srcH) };

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            if (xScale == 2)
                dst[x + y * dstW] = src[(x >> 1) + (y >> 1) * srcW];
            else
                dst[x + y * dstW] = src[x + (y >> 1) * srcW];
        }
    }
}

int main()
{
    try {
        extern void TestDebugger();
        TestDebugger();

        //constexpr const DecodeTestCase tests[] = {
        //    { "67668C6199", "MOV\t[ECX-0x67], FS" }, // N.B. unused operand-size prefix
        //};
        //RunTests(CPUInfo { CPUModel::i80386, 2 }, tests);

        TestDecode16(CPUModel::i8088);
        TestDecode16(CPUModel::i8086);
        TestDecode16(CPUModel::i80386);
        TestDecode32(CPUModel::i80386);

        extern void TestMoo();
        //TestMoo();

        const int guiWidth = 640;
        const int guiHeight = 400;

        GUI gui { guiWidth, guiHeight };
        SetGuiActive(true);

#if 0
        Test386Machine machine { "../misc/test386.asm/test386.bin" };
#else
        XTMachine machine {};
        auto& bus = machine.bus;

        //auto rom = RomHandler { ReadFile("../misc/pcxtbios/pcxtbios.bin") };
        auto rom = RomHandler { ReadFile("../misc/GLABIOS/GLABIOS_0.4.1_8X.ROM") };
        // auto rom = RomHandler { ReadFile("../misc/asmtest/gfxtest/test.com") };
        bus.addMemHandler(0x100000 - rom.size(), rom.size(), rom);

        std::vector<uint32_t> screenBuffer(guiWidth * guiHeight);
        machine.cga.setDrawFunction([&screenBuffer](const uint32_t* pixels, int w, int h) {
            StretchImage(&screenBuffer[0], guiWidth, guiHeight, pixels, w, h);
            DrawScreen(screenBuffer.data());
        });

        auto& floppy = machine.floppy;
        floppy.insertDisk(0, ReadFile(R"(../misc/sw/small.img)"));
#endif
        Debugger dbg { machine.cpu, machine.bus };
        auto& cpu = machine.cpu;

        //dbg.activate();
        //dbg.addBreakPoint((0xf000 << 4) + 0xEE6F);

        bool quit = false;
        for (unsigned guiUpdateCnt = 0; !quit;) {

            if (guiUpdateCnt-- == 0) {
                guiUpdateCnt = 10000;
                for (const auto& evt : gui.update()) {
                    switch (evt.type) {
                    case GUI::EventType::quit:
                        quit = true;
                        break;
                    case GUI::EventType::keyboard:
                        machine.keyboardEvent(evt.key);
                        break;
                    default:
                        throw std::runtime_error { "TODO: Handle event type + " + std::to_string((int)evt.type) };
                    }
                }
            }

            dbg.check();
            try {
                cpu.step();
                //if (cpu.ip_ == 0x0000D58B) // Can hang if failure is in ring 3
                //    throw std::runtime_error { "ERROR" };
            } catch (const std::exception& e) {
                const char* const sep = "---------------------------------------------------";
                std::println("{}", sep);
                //cpu.showHistory();
                //std::println("");
                //cpu.trace();
                std::println("Halted after {} instructions", cpu.instructionsExecuted());
                std::println("{}", e.what());
                std::println("{}", sep);
                dbg.activate();
            }
        }
    } catch (const std::exception& e) {
        std::println("Exception caught: {}", e.what());
    }

}
