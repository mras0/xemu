#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "cpu.h"
#include "system_bus.h"

class DebuggerMemState {
public:
    DebuggerMemState()
        : address { 0, 0, 2 }
    {
    }
    // TODO: How to handle use of linear/physical addresses
    // TODO: Maybe save a descriptor?
    Address address;
};

class Debugger {
public:
    explicit Debugger(CPU& cpu, SystemBus& bus);

    void check(void);
    void commandLoop(void);
    void activate();
    void addBreakPoint(std::uint64_t physicalAddress);

private:
    struct BreakPoint {
        bool active = false;
        uint64_t phys;
    };
    static constexpr size_t maxBreakPoints = 8;

    CPU& cpu_;
    SystemBus bus_;
    bool active_ = false;
    DebuggerMemState disAsmAddr_;
    DebuggerMemState hexDumpAddr_;
    BreakPoint breakPoints_[maxBreakPoints];
    BreakPoint autoBreakPoint_;
    uint32_t traceCount_ = 0;

    bool checkBreakPoint(const BreakPoint& bp);
        
    bool handleLine(const std::string& line);
    void initMemState(DebuggerMemState& ms, uint16_t seg, uint64_t offset);
    uint64_t toPhys(const DebuggerMemState& ms, uint64_t offset);
};

#endif
