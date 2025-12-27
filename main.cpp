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
#include "devs/vga.h"
#include "devs/i8259a_pic.h"
#include "devs/i8253_pit.h"
#include "devs/nec765_floppy_controller.h"
#include "devs/i8237a_dma_controller.h"
#include "devs/i8042_ps2_controller.h"
#include "devs/ata_controller.h"
#include "bios_replacement.h"
#include "disk_data.h"

#define USE_EGA

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

    virtual void keyboardEvent(const KeyPress& key)
    {
        std::println("Ignoring key scanCode=0x{:02X} down={}", key.scanCode, key.down);
    }

    virtual void forceRedraw() { }
};

static constexpr bool isCommPort(uint16_t port)
{
    // MDA LPT1
    if (port >= 0x3BC && port <= 0x3BF)
        return true;

    switch (port & 0xFFF8) {
    case 0x3F8: // COM1
    case 0x2F8: // COM2
    case 0x3E8: // COM3
    case 0x2E8: // COM4
    case 0x378: // LPT1
    case 0x278: // LPT2
        return true;
    default:
        return false;
    }
}

static constexpr bool isATAPort(uint16_t port)
{
    switch (port & 0xFFF8) {
        // ATA 1
    case 0x1f0:
    case 0x3f0:
        return true;
        // ATA 2
    case 0x170:
    case 0x370:
        return true;
        // ATA 3
    case 0x1e8:
    case 0x3e0:
        return true;
        // ATA 4
    case 0x168:
    case 0x360:
        return true;
    default:
        return false;
    }
}

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
        , dma { bus, 0x00, 0x81, false }
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

    void keyboardEvent(const KeyPress& key) override
    {
        ppi.enqueueScancode(static_cast<uint8_t>(key.scanCode | (key.down ? 0 : 0x80)));
    }

private:
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

    void set(uint8_t index, uint8_t value)
    {
        assert(index < data_.size());
        data_[index] = value;
    }

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override
    {
        switch (offset) {
        case 1:
            std::println("CMOS: Read from reg {:02X} -> {:02X}", reg_ & indexMask, data_[reg_ & indexMask]);
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

class BochsDebugHandler : public IOHandler {
public:
    explicit BochsDebugHandler(SystemBus& bus)
    {
        bus.addIOHandler(0x80, 1, *this); // PORT_DIAG
        bus.addIOHandler(0x400, numLevels, *this); // PANIC_PORT, ...
    }

private:
    static constexpr std::uint16_t numLevels = 4;
    static constexpr const char* const desc_[numLevels] = {
        "PANIC", "PANIC2", "INFO", "DEBUG"
    };
    std::string buffers_[numLevels];
    std::uint8_t diagLast_ = 0xcd;
    std::uint32_t diagCount_ = 0;

    void outU8([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset, std::uint8_t value) override
    {
        if (port == 0x80) {
            if (value != diagLast_) {
                std::print("BOCHS diag: 0x{:02X}", value);
                if (diagCount_)
                    std::print(" ({} times 0x{:02X} ignored)", diagCount_, diagLast_);
                std::println("");
                diagLast_ = value;
                diagCount_ = 0;
            } else {
                ++diagCount_;
            }
            return;
        }

        assert(offset < numLevels);
        auto& buf = buffers_[offset];
        if (value == 10) {
            std::println("BOCHS {}: {}", desc_[offset], buf);
            buf.clear();
        } else {
            buf.push_back(value);
        }
    }
};

class PCIHandler : public IOHandler {
public:
    explicit PCIHandler(SystemBus& bus)
    {
        bus.addIOHandler(0x4D0, 2, *this); // IRQ
        bus.addIOHandler(0xCF8, 8, *this); // Config
    }

    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override
    {
        if (port == 0x4D0 || port == 0x4D1) {
            std::println("PCI: Ignoring IRQ config write to port {:04X} {:02X}", port, value);
            return;
        }
        IOHandler::outU8(port, offset, value);
    }

    void outU32(std::uint16_t port, std::uint16_t offset, std::uint32_t value) override
    {
        if (offset & 3)
            return IOHandler::outU32(port, offset, value);
        if (offset != 0)
            throw std::runtime_error { std::format("PCI write to address {:08X} value {:08X}", address_, value) };
        std::println("PCI: Selecting address {:08X}", value);
        address_ = value;
    }

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override
    {
        if (offset < 4)
            return IOHandler::inU8(port, offset);
        return 0xff;
    }

private:
    uint32_t address_ = 0;
};

class A20Control : public IOHandler {
public:
    explicit A20Control(SystemBus& bus)
        : bus_ { bus }
    {
        bus.addIOHandler(0x92, 1, *this);
        setState(false);
    }

    uint8_t inU8([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset) override
    {
        return fastA20_ ? PORTA_MASK_A20 : 0;
    }

    void outU8([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset, std::uint8_t value) override
    {
        //std::println("Output to fast A20 port: {:02X}", value);
        if (value & ~PORTA_MASK_A20)
            throw std::runtime_error { std::format("Unsupported value written to port 0x92 (Fast A20): {:02X}", value) };
        fastA20_ = (value & PORTA_MASK_A20) != 0;
        setA20State();
    }

    void setKbdA20Line(bool value)
    {
        kbdA20Line_ = value;
        setA20State();
    }

private:
    static constexpr uint8_t PORTA_MASK_A20 = 1 << 1;

    SystemBus& bus_;
    bool curState_ = false;
    bool kbdA20Line_ = false;
    bool fastA20_ = false;

    void setState(bool enabled)
    {
        //std::println("A20 gate {}!", enabled ? "enabled" : "disabled");
        bus_.setAddressMask(UINT64_MAX & ~(enabled ? 0 : 1 << 20));
        curState_ = enabled;
    }


    void setA20State()
    {
        const bool enabled = kbdA20Line_ || fastA20_;
        if (enabled != curState_)
            setState(enabled);
    }
};

class Clone386Machine : public BaseMachine, public IOHandler {
public:
    explicit Clone386Machine()
        : BaseMachine { CPUModel::i80386sx }
        , extendedMem { 15 * 1024 * 1024 }
        , a20control { bus }
        , cmos { bus }
        , dma1 { bus, 0x00, 0x81, false }
        , dma2 { bus, 0xC0, 0x89, true }
        , video { bus }
        , pic1 { bus, 0x20 }
        , pic2 { bus, 0xA0 }
        , pit { bus,
            [this]() {
                // std::println("PIT interrupt");
                pic1.setInterrupt(PIC_IRQ_PIT);
            } }
        , ps2 { bus, 
            [this]() {
                std::println("Keyboard interrupt");
                pic1.setInterrupt(PIC_IRQ_KEYBOARD);
               },
            [this](bool value) {
                a20control.setKbdA20Line(value);
            }
        }
        , floppy {
            bus,
            [this]() { pic1.setInterrupt(PIC_IRQ_FLOPPY); },
            [this](bool isPut, DMAHandler& handler) {
                assert(!isPut);
                (void)isPut;
                dma1.startGet(DMA_CHANNEL_FLOPPY, handler);
            },
            true, // ATA needs ports 0x3f6/0x3f7
        }
        , ata1 { bus, 0x1f0, 0x3f6, []() {
                    throw std::runtime_error { "TODO: ATA1 IRQ!" };
                } }
    {
        bus.setDefaultIOHandler(this);
        cpu.setInterruptFunction([this]() { return pic1.getInterrupt(); });
        pic1.addSlave(pic2);
        bus.addMemHandler(1024 * 1024, extendedMem.size(), extendedMem);
    }

    RamHandler extendedMem;
    A20Control a20control;
    CMOS cmos;
    i8237a_DMAController dma1;
    i8237a_DMAController dma2;
#ifdef USE_EGA
    VGA video;
#else
    CGA video;
#endif
    i8259a_PIC pic1;
    i8259a_PIC pic2;
    i8253_PIT pit;
    i8042_PS2Controller ps2;
    NEC765_FloppyController floppy;
    ATAController ata1;
    std::string serialData;

    void forceRedraw() override
    {
        video.forceRedraw();
    }

    void keyboardEvent(const KeyPress& key) override
    {
        ps2.enqueueKey(key);
    }

    uint8_t inU8(std::uint16_t port, [[maybe_unused]] std::uint16_t offset) override
    {
        if (isCommPort(port) || isATAPort(port))
            return 0xFF;

        if (port == 0x201) // Game port
            return 0xFF;

        if (port == 0x1CF) // SEA bios
            return 0xFF;

        if (port == 0xA20 || port == 0xA24) { // ??? Power management? (Win3.1)
            std::println("Ignoring read from port {:04X}", port);
            return 0xFF;
        }


        // Win3.1 install ?? 0x23x is BUS mouse
        // 3BA MDA
        if ((port >= 0x238 && port <= 0x23F) || port == 0x3BA || port >= 0x1000) {
            std::println("Ignoring read from port {:04X}", port);
            return 0xFF;
        }

        return IOHandler::inU8(port, offset);
    }

    uint32_t inU32(std::uint16_t port, [[maybe_unused]] std::uint16_t offset) override
    {
        // FreeDOS probes this for "VMX"
        if (port == 0x5658)
            return UINT32_MAX;
        return IOHandler::inU32(port, offset);
    }

    void outU8(std::uint16_t port, [[maybe_unused]] std::uint16_t offset, std::uint8_t value) override
    {
        if ((port & 0x3f8) == 0x3f8) {
            // Serial port
            if (port == 0x3f8) {
                if (value != 0x0C) // Sent by windows debug build
                    serialData += value;
                if (value == '\n') {
                    std::println("Serial data: {:?}", serialData);
                    serialData.clear();
                }
            }
            return;
        }
        std::println("Ignoring write to port {:02X} value {:02X}", port, value);
        if (isCommPort(port) || isATAPort(port))
            return;

        //switch (port) {
        //case 0x80:
        //case 0xE1:
// 
        //case 0xE2:
        //case 0xEA:
        //case 0xEB:
        //case 0xEC:
        //case 0x8022: // XXX???
        //    std::println("Ignoring write to port {:02X} value {:02X}", port, value);
        //    break;
        //default:
        //    IOHandler::outU8(port, offset, value);
        //}
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
    if ((xScale != 1 && xScale != 2) || yScale != 2) {
        int32_t dudx = (srcW << 16) / dstW, dvdy = (srcH << 16) / dstH;

        int32_t v = 0;
        for (int y = 0; y < dstH; ++y) {
            int32_t u = 0;
            for (int x = 0; x < dstW; ++x) {
                dst[x + y * dstW] = src[(u >> 16) + (v >> 16) * srcW];
                u += dudx;
            }
            v += dvdy;
        }

        return;
    }

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

        const int guiWidth = 800;
        const int guiHeight = 600;

        GUI gui { guiWidth, guiHeight };
        SetGuiActive(true);
        [[maybe_unused]] std::vector<uint32_t> screenBuffer(guiWidth * guiHeight);


        std::function<void(uint8_t, std::string_view)> diskInsertionEvent = [](uint8_t drive, std::string_view filename) {
            throw std::runtime_error { std::format("No support for disk insertion in drive {:02X} {:?}", drive, filename) };
        };

        Clone386Machine machine;
        machine.video.setDrawFunction([&screenBuffer](const uint32_t* pixels, int w, int h) {
            if (!pixels) {
                // No sync
                for (int y = 0; y < guiHeight; ++y) {
                    for (int x = 0; x < guiWidth; ++x) {
                        screenBuffer[x + y * guiWidth] = ((x >> 2) ^ (y >> 2)) & 1 ? 0x555555 : 0x111111;
                    }
                }
                DrawScreen(screenBuffer.data());
                return;
            }
            StretchImage(&screenBuffer[0], guiWidth, guiHeight, pixels, w, h);
            DrawScreen(screenBuffer.data());
        });

        const char* diskName = "../misc/asmtest/egagfx/test.img";
       
        try {
            CreateDisk("hd.bin", diskFormatSL520);
            std::println("Created HD");
        } catch (...) {
        }
#ifdef USE_EGA
        auto videoRomData = ReadFile(R"(../misc/ega/ega.rom)");
        //auto videoRomData = ReadFile(R"(c:\Tools\bochs-2.7\bios\VGABIOS-lgpl-latest)");
#else
        auto videoRomData = ReadFile(R"(bios/videobios.bin)");
#endif
        auto videoRom = RomHandler { videoRomData };
        machine.bus.addMemHandler(0xC0000, videoRom.size(), videoRom);

        auto rom = RomHandler { ReadFile(R"(c:\Tools\bochs-2.7\bios\BIOS-bochs-legacy)") };
        BochsDebugHandler bochsDbgHandler { machine.bus };
        PCIHandler pciHandler { machine.bus };
        machine.cmos.set(0x10, 0x44); // 2x1.44MB floppy drives

        assert(machine.extendedMem.size() < 16ULL * 1024 * 1024); // TODO: CMOS 0x34/0x35 Extended Mem size in 64K blocks > 16MB
        const auto extMemSize = std::min(size_t(63 * 1024), machine.extendedMem.size() >> 10); // In KB
        machine.cmos.set(0x30, static_cast<uint8_t>(extMemSize & 0xff));
        machine.cmos.set(0x31, static_cast<uint8_t>(extMemSize >> 8)); 


        //machine.cmos.set(0x3D, 0x01); // Boot from floppy
        machine.floppy.insertDisk(0, ReadFile(diskName));
        ////machine.cmos.set(0x3D, 0x21); // Boot from floppy then HD
        machine.cmos.set(0x3D, 0x02); // Boot from HD
        machine.ata1.insertDisk(0, "hd.bin");
        //machine.ata1.insertDisk(0, "win2.bin");
        //machine.ata1.insertDisk(0, "freedos.bin");
        
        machine.bus.addMemHandler(0x100000 - rom.size(), rom.size(), rom);


        // Ignore unmapped ROM area
        struct IgnoredHandler : public MemoryHandler {
            IgnoredHandler(SystemBus& bus, uint64_t start, uint64_t end) { bus.addMemHandler(start, end - start, *this); }
            std::uint8_t readU8([[maybe_unused]] std::uint64_t addr, [[maybe_unused]] std::uint64_t offset) override { return 0xFF; }
            void writeU8([[maybe_unused]] std::uint64_t addr, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] std::uint8_t value) override { }
        } ignoredHandler {machine.bus, 0xC0000+videoRom.size(), 0x100000 - rom.size() };

        diskInsertionEvent = [&](uint8_t drive, std::string_view filename) {
            if (!filename.empty()) {
                std::println("Inserting in drive {:02X}: {:?}", drive, filename);
                machine.floppy.insertDisk(drive, ReadFile(std::string(filename)));
            } else {
                std::println("Ejecting disk in drive {:02X}", drive, filename);
                machine.floppy.insertDisk(drive, filename);

            }
        };

        Debugger dbg { machine.cpu, machine.bus };
        auto& cpu = machine.cpu;

        struct DebugBreakHandler : public IOHandler {
            enum : uint16_t { magicPort = 0x8abc };
            explicit DebugBreakHandler(SystemBus& bus, Debugger& dbg)
                : dbg_ { dbg }
            {
                bus.addIOHandler(magicPort, 2, *this);
            }

            void outU16(uint16_t port, uint16_t, uint16_t value) override
            {
                if (value == static_cast<uint16_t>(~magicPort))
                    dbg_.activate();
                else
                    throw std::runtime_error { std::format("Invalid value written to debug port {:X}: {:X}", port, value) };
            }

            Debugger& dbg_;
        } dbgBreakHandler { machine.bus, dbg };

        dbg.setOnActive([&](bool active) {
            if (active)
                machine.forceRedraw();
            SetGuiActive(!active);
        });
        machine.video.registerDebugFunction(dbg);

        //dbg.activate();
        //dbg.addBreakPoint((0xC000 << 4) + 0x448); // POD14_ERR
        //dbg.addBreakPoint((0xC000 << 4) + 0x4E0); // "HOW_BIG"
        //dbg.addBreakPoint((0xC000 << 4) + 0x630);
        //dbg.addBreakPoint((0xC000 << 4) + 0x660); // PODSTG_ERR0
        //dbg.addBreakPoint((0xC000 << 4) + 0x5c3);

        //machine.cpu.exceptionTraceMask(0);

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
                    case GUI::EventType::diskInsert:
                        diskInsertionEvent(evt.diskInsert.drive, evt.diskInsert.filename);
                        break;
                    case GUI::EventType::diskEject:
                        diskInsertionEvent(evt.diskEject.drive, {});
                        break;
                    default:
                        throw std::runtime_error { "TODO: Handle event type + " + std::to_string((int)evt.type) };
                    }
                }
            }

            dbg.check();
            try {
                //if (cpu.ip_ == 0x02CD)
                //    __nop();
                cpu.step();
                //if (cpu.protectedMode() && (cpu.sdesc_[SREG_CS].flags & SD_FLAGS_MASK_DB)) {
                //    static bool first = true;
                //    if (first) {
                //        first = false;
                //        dbg.activate();
                //    }
                //}
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
