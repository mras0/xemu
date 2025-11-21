#include "i8259a_pic.h"
#include <cassert>
#include <format>
#include <print>

static constexpr uint8_t ICW1_MASK_ICW4 = 1 << 0;
static constexpr uint8_t ICW1_MASK_SINGLE = 1 << 1; // Single (no ICW3)/cascade mode
static constexpr uint8_t ICW1_MASK_INTERVAL4 = 1 << 2; // Call interval 4/8
static constexpr uint8_t ICW1_MASK_LEVEL = 1 << 3; // Level/edge triggered
static constexpr uint8_t ICW1_MASK_INIT = 1 << 4;

static constexpr uint8_t ICW4_MASK_8086 = 1 << 0;
static constexpr uint8_t ICW4_MASK_SFNM = 1 << 3;

i8259a_PIC::i8259a_PIC(SystemBus& bus, uint16_t ioBase)
{
    bus.addIOHandler(ioBase, 2, *this, true);
    reset();
}

void i8259a_PIC::reset()
{
    icwCnt_ = 1;
    icw1_ = 0;
    icw2_ = 0;
    icw3_ = 0;
    icw4_ = 0;
    irr_ = 0;
    isr_ = 0;
    imr_ = 0xff;
    nextReg_ = 0;
}

std::uint8_t i8259a_PIC::inU8(uint16_t port, uint16_t offset)
{
    if (offset == 0)
        return nextReg_ ? isr_ : irr_;
    else if (offset == 1)
        return imr_; // AKA OCW1
    return IOHandler::inU8(port, offset);
}

void i8259a_PIC::outU8(uint16_t port, uint16_t offset, std::uint8_t value)
{
    const char* const name = (port & ~1) == 0x20 ? "PIC1" : "PIC2";

    if (offset == 0) {
        // Command
        if (value & ICW1_MASK_INIT) {
            if ((value & ~ICW1_MASK_INIT) != (ICW1_MASK_SINGLE | ICW1_MASK_ICW4))
                throw std::runtime_error { std::format("{}: Unsupported ICW1: {:02X}", name, value) };
            icw1_ = value;
            icwCnt_ = 2;
            std::println("{}: ICW1={:02X}", name, value);
        } else {
            // OCW2/3 depending on bit3
            if (value & 8) {
                // OCW3
                switch (value) {
                case 0b1010: // read ISR
                    nextReg_ = 0;
                    return;
                case 0b1011: // read IRR
                    nextReg_ = 1;
                    return;
                }
            } else {
                // OCW2
                if (value == 0x20) { // non-specific EOI
                    // The highest request level is reset from the IRR when an interrupt is acknowledged.
                    isr_ = 0; // XXX
                    return;
                }
            }
            throw std::runtime_error { std::format("{}: Unsupported write to OCW{}: {:02X}", name, value & 8 ? 3 : 2, value) };
        }
    } else {
        // Data
        if (icwCnt_) {
            switch (icwCnt_) {
            case 2:
                if (value & 7)
                    throw std::runtime_error { std::format("{}: Invalid ICW2: {:02X}", name, value) };
                icw2_ = value;
                std::println("{}: ICW2={:02X}", name, value);
                if (icw1_ & ICW1_MASK_SINGLE) {
                    icwCnt_ = icw1_ & ICW1_MASK_ICW4 ? 4 : 0;
                } else {
                    ++icwCnt_;
                }
                break;
            case 3:
                assert(!(icw1_ & ICW1_MASK_SINGLE));
                std::println("{}: ICW3={:02X}", name, value);
                icw3_ = value;
                icwCnt_ = icw1_ & ICW1_MASK_ICW4 ? 4 : 0;
                break;
            case 4:
                assert(icw1_ & ICW1_MASK_ICW4);
                icw4_ = value;
                icwCnt_ = 0;
                std::println("{}: ICW4={:02X}", name, value);
                if ((value & ~ICW4_MASK_SFNM) != ICW4_MASK_8086)
                    throw std::runtime_error { std::format("{}: Unsupported ICW4: {:02X}", name, value) };
                break;
            default:
                throw std::runtime_error { std::format("{}: Not ready (icw_cnt {}): {:02X}", name, icwCnt_, value) };
            }
            if (!icwCnt_)
                std::println("{}: Ready!", name);
        } else {
            std::println("{}: IMR={:02X} 0b{:08b}", name, value, value);
            imr_ = value;
        }
    }
}

int i8259a_PIC::getInterrupt()
{
    if (icwCnt_)
        return -1;
    auto pending = irr_ & ~imr_;
    if (!pending)
        return -1;

    for (int i = 0; i < 8; ++i) {
        const uint8_t mask = 1 << i;
        if (pending & mask) {
            irr_ &= ~mask;
            isr_ |= mask;
            //std::println("PIC: IRQ {}", i);
            return i | icw2_;
        }
    }
    throw std::runtime_error { std::format("Internal error in getInterrupt, pending = {}", pending) };
}

void i8259a_PIC::setInterrupt(std::uint8_t line)
{
    assert(line < 8);
    irr_ |= 1 << line;
}

void i8259a_PIC::clearInterrupt(std::uint8_t line)
{
    assert(line < 8);
    irr_ &= ~(1 << line);
}
