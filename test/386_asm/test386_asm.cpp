#include "cpu.h"
#include "system_bus.h"
#include "util.h"
#include "fileio.h"
#include <print>
#include <fstream>

static std::string test386Dir = "../../../misc/test386.asm/";

class Test386Machine : public IOHandler {
public:
    explicit Test386Machine()
        : bus {}
        , cpu { CPUModel::i80586, bus } // Pretends to be 386 but tests undocumented ss > 0
        , conventionalMem { 640 * 1024 }
        , rom_ { ReadFile(test386Dir + "test386.bin") }
        , debugFile_ { "out.txt", std::ofstream::binary }
    {
        bus.addIOHandler(debugPort, 1, *this);
        bus.addIOHandler(postPort, 1, *this);
        bus.addMemHandler(0, conventionalMem.size(), conventionalMem);
        bus.addMemHandler(1024 * 1024 - rom_.size(), rom_.size(), rom_);
    }

    SystemBus bus;
    CPU cpu;

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
                const std::string compareCommand = "comp /M /L";
#else
                const std::string compareCommand = "diff";
#endif

                exit(system((compareCommand + " out.txt \"" + test386Dir + "test386-EE-reference.txt\"").c_str()));
            }
            break;
        default:
            IOHandler::outU8(port, port, value);
        }
    }

private:
    static constexpr uint16_t debugPort = 0xe9;
    static constexpr uint16_t postPort = 0x190;

    RamHandler conventionalMem;
    RomHandler rom_;
    std::string debugBuffer_;
    std::ofstream debugFile_;
};


int main()
{
    try {
        Test386Machine machine {};
        auto& cpu = machine.cpu;
        try {
            for (;;)
                cpu.step();
        } catch (const std::exception& e) {
            const char* const sep = "---------------------------------------------------";
            std::println("{}", sep);
            cpu.showHistory();
            std::println("");
            cpu.trace();
            std::println("");
            std::println("Halted after {} instructions", cpu.instructionsExecuted());
            std::println("{}", e.what());
            std::println("{}", sep);
            return 1;
        }
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
