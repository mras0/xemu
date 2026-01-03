#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <functional>
#include <optional>
#include <map>

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

class DebuggerInterface {
public:
    virtual bool moreArgs() = 0;
    virtual std::optional<std::uint64_t> getNumber() = 0;
    virtual std::optional<std::string> getString() = 0;
};

class Debugger {
public:
    explicit Debugger(CPU& cpu, SystemBus& bus);

    using FunctionCallback = std::function<void (DebuggerInterface&, std::string_view)>;

    void check(void);
    void commandLoop(void);
    void activate();
    void addPhysicalBreakPoint(std::uint64_t physicalAddress);
    void addBreakPoint(std::uint16_t segment, std::uint64_t offset);
    void setOnActive(const std::function<void(bool)>& onSetActive)
    {
        onSetActive_ = onSetActive;
    }

    void registerFunction(const std::string& name, const FunctionCallback& callback);

private:
    struct BreakPoint {
        enum Type { INACTIVE, PHYSICAL, LOGICAL} type;
        uint16_t seg;
        uint64_t address;
    };
    static constexpr size_t maxBreakPoints = 8;

    CPU& cpu_;
    SystemBus& bus_;
    bool active_ = false;
    DebuggerMemState disAsmAddr_;
    DebuggerMemState hexDumpAddr_;
    BreakPoint breakPoints_[maxBreakPoints];
    BreakPoint autoBreakPoint_;
    uint32_t traceCount_ = 0;
    std::function<void (bool)> onSetActive_;
    std::map<std::string, FunctionCallback> functions_;

    BreakPoint& getFreeBreakPoint();
    bool checkBreakPoint(const BreakPoint& bp);
        
    bool handleLine(const std::string& line);
    void initMemState(DebuggerMemState& ms, SReg sr, uint64_t offset, uint8_t addressSize = 0);
    uint64_t toPhys(const DebuggerMemState& ms, uint64_t offset);
    uint64_t toPhys(uint64_t linearAddress);
    uint64_t peekMem(uint64_t physAddress, size_t size);
    uint64_t peekMemLinear(uint64_t lienarAddress, size_t size);
    uint64_t peekDescriptor(uint16_t sel);
    uint64_t getPhysicalIp(const CPUState& st);
};

#endif
