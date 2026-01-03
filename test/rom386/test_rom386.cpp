#include "cpu.h"
#include "system_bus.h"
#include "util.h"
#include "fileio.h"
#include "debugger.h"
#include <print>

static bool debugBreak;

class Test386Machine : public IOHandler {
public:
    explicit Test386Machine()
        : bus {}
        , cpu { CPUModel::i80386, bus }
        , conventionalMem_ { 640 * 1024 }
        , expandedMem_ { 3 * 1024 * 1024 }
        , gfxMem_ { 80*25*2 }
        , rom_ { ReadFile("test_rom386.bin") }
    {
        bus.addMemHandler(0, conventionalMem_.size(), conventionalMem_);
        bus.addMemHandler(0xb8000, gfxMem_.size(), gfxMem_);
        bus.addMemHandler(1024 * 1024, expandedMem_.size(), expandedMem_);
        bus.addMemHandler(1024 * 1024 - rom_.size(), rom_.size(), rom_);
        bus.setDefaultIOHandler(this);
    }

    SystemBus bus;
    CPU cpu;

    static bool isIgnoredPort(uint16_t port)
    {
        return (port >= 0x3D0 && port <= 0x3DF) || // CGA
            (port >= 0x3F8 && port <= 0x3FF); // serial
    }

    uint8_t inU8(uint16_t port, uint16_t) override
    {
        if (isIgnoredPort(port))
            return 0xFF;
        return IOHandler::inU8(port, port);
    }

    void outU8(uint16_t port, uint16_t, std::uint8_t value) override
    {
        if (isIgnoredPort(port))
            return;
        switch (port) {
        case debugPort:
            if (value == '\n') {
                std::println("{}", debugBuffer_);
                debugBuffer_.clear();
            } else if (value != '\r')
                debugBuffer_ += value;
            break;
        case postPort:
            std::println("POST: 0x{:02X}", value);
            if (value == 0xff) {
                std::println("Success!");
                exit(0);
            }
            break;
        case breakPort:
        case breakPort+1:
            debugBreak = true;
            break;
        case 0x400: // Used by test
        case 0x4FE:
        case 0x4FF:
            return;
        case 0x21: // interrupt mask registers
        case 0xA1:
            if (value == 0xff)
                return;
            [[fallthrough]];
        default:
            IOHandler::outU8(port, port, value);
        }
    }

private:
    static constexpr uint16_t debugPort = 0xe9;
    static constexpr uint16_t postPort = 0x190;
    static constexpr uint16_t breakPort = 0x8abc;

    RamHandler conventionalMem_;
    RamHandler expandedMem_;
    RamHandler gfxMem_;
    RomHandler rom_;
    std::string debugBuffer_;
};

int main()
{
    try {
        Test386Machine machine {};
        auto& cpu = machine.cpu;
        Debugger dbg { cpu, machine.bus };
        if (!IsStdioInteractive()) {
            dbg.setOnActive([](bool active) {
                if (active)
                    exit(1);
            });
        }
        for (;;) {
            try {
                if (debugBreak) {
                    dbg.activate();
                    debugBreak = false;
                }
                dbg.check();
                cpu.step();
            } catch (const std::exception& e) {
                const char* const sep = "---------------------------------------------------";
                std::println("{}", sep);
                cpu.showHistory(stdout, 2);
                std::println("");
                cpu.trace(stdout);
                std::println("");
                std::println("Halted after {} instructions", cpu.instructionsExecuted());
                std::println("{}", e.what());
                std::println("{}", sep);
                dbg.activate();
            }
        }
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
