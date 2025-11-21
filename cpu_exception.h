#ifndef CPU_EXCEPTION_H
#define CPU_EXCEPTION_H

#include <stdexcept>
#include <cassert>
#include <string>
#include <cstdint>

enum CPUExceptionNumber : std::uint8_t {
    DivisionError, // #DE
    Debug, // #DB
    NMI,
    Breakpoint, // #BP
    Overflow, // #OF
    BoundRangeExceeded, // #BR
    InvalidOpcode, // #UD
    NoMathCoprocessor, // #NM
    DoubleFault, // #DF
    Reserved9,
    InvalidTSS, // #TS
    SegmentNotPresent, // #NP
    StackSegmentFault, // #SS
    GeneralProtection, // #GP
    PageFault, // #PF

    ExceptionMax
};

constexpr uint32_t CPUExceptionErrorCodeMask = 1 << 8 | 1 << 10 | 1 << 11 | 1 << 12 | 1 << 13 | 1 << 14 | 1 << 17 | 1 << 30;

extern const char* const CPUExceptionNumberShortText[];
extern const char* const CPUExceptionNumberText[];


static constexpr int ExceptionNone = -1;
static constexpr int ExceptionNumberMask = 0xff;
static constexpr int ExceptionHardwareMask = 0x100;

class CPUException : public std::exception {
public:
    explicit CPUException(CPUExceptionNumber exceptionNo, std::uint32_t errorCode = 0);

    const char* what() const noexcept override
    {
        return description_.c_str();
    }

    CPUExceptionNumber exceptionNo() const
    {
        return exceptionNo_;
    }

    bool hasErrorCode() const
    {
        return exceptionNo_ < 32 && (CPUExceptionErrorCodeMask & (1 << exceptionNo_));
    }

    std::uint32_t errorCode() const
    {
        assert(hasErrorCode());
        return errorCode_;
    }

private:
    CPUExceptionNumber exceptionNo_;
    std::string description_;
    std::uint32_t errorCode_;
};

std::string FormatExceptionNumber(int exceptionNo);

#endif
