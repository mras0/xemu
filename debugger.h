#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <functional>

#include "cpu.h"
#include "system_bus.h"

class DebuggerMemState {
public:
    DebuggerMemState();
    // TODO: How to handle use of linear/physical addresses
    // TODO: Maybe save a descriptor?
    int sr;
    Address address;
};

class Debugger {
public:
    explicit Debugger(CPU& cpu, SystemBus& bus);

    void check(void);
    void commandLoop(void);
    void activate();
    void addBreakPoint(std::uint64_t physicalAddress);
    void setOnActive(const std::function<void(bool)>& onSetActive)
    {
        onSetActive_ = onSetActive;
    }

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
    std::function<void (bool)> onSetActive_;

    bool checkBreakPoint(const BreakPoint& bp);
        
    bool handleLine(const std::string& line);
    void initMemState(DebuggerMemState& ms, SReg sr, uint64_t offset);
    uint64_t toPhys(const DebuggerMemState& ms, uint64_t offset);
    uint64_t toPhys(uint64_t linearAddress);
    uint64_t peekMem(uint64_t physAddress, size_t size);
};

#endif
