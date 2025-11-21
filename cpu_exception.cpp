#include "cpu_exception.h"
#include <format>

static_assert(static_cast<int>(InvalidOpcode) == 6);
static_assert(static_cast<int>(GeneralProtection) == 13);

const char* const CPUExceptionNumberText[] = {
    "Divsion Error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "No Math Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection",
    "Page Fault",
};
static_assert(std::size(CPUExceptionNumberText) == CPUExceptionNumber::ExceptionMax);

const char* const CPUExceptionNumberShortText[] = {
    "#DE",
    "#DB",
    "NMI",
    "#BP",
    "#OF",
    "#BR",
    "#UD",
    "#NM",
    "#DF",
    "#E9",
    "#TS",
    "#NP",
    "#SS",
    "#GP",
    "#PF",
};
static_assert(std::size(CPUExceptionNumberShortText) == CPUExceptionNumber::ExceptionMax);

CPUException::CPUException(CPUExceptionNumber exceptionNo, std::uint32_t errorCode)
    : exceptionNo_ { exceptionNo }
    , description_ { std::format("CPUException(0x{:02X}) - {} {}{}", static_cast<std::uint8_t>(exceptionNo), CPUExceptionNumberShortText[exceptionNo], CPUExceptionNumberText[exceptionNo],  exceptionNo < 32 && (CPUExceptionErrorCodeMask & (1 << exceptionNo)) ? std::format(" ErrorCode 0x{:08X}", errorCode) : "") }
    , errorCode_ { errorCode }
{
    assert(errorCode == 0 || hasErrorCode());
}


std::string FormatExceptionNumber(int exceptionNo)
{
    assert(exceptionNo >= 0 && exceptionNo < (0x100 | CPUExceptionNumber::ExceptionMax));
    if (exceptionNo & ExceptionHardwareMask) {
        const auto no = exceptionNo & ExceptionNumberMask; 
        if (no < ExceptionMax)
            return std::format("Exception {} {} {}", no, CPUExceptionNumberShortText[no], CPUExceptionNumberText[no]);
        else
            return std::format("Exception {}", no);
    } else {
        return std::format("Interrupt {}", exceptionNo);
    }
}
