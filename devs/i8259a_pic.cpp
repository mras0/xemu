#include "i8259a_pic.h"
#include <cassert>
#include <format>
#include <print>

#if 0
#define LOG(...) std::println(__VA_ARGS__)
#else
#define LOG(...)
#endif

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
            if ((value & ~(ICW1_MASK_INIT | ICW1_MASK_SINGLE)) != ICW1_MASK_ICW4)
                throw std::runtime_error { std::format("{}: Unsupported ICW1: {:02X}", name, value) };
            if (!(value & ICW1_MASK_SINGLE) && !companion_)
                throw std::runtime_error { std::format("{}: Unsupported ICW1: {:02X} - Configured in cascade mode without master/slave", name, value) };
            icw1_ = value;
            icwCnt_ = 2;
            std::println("{}: ICW1={:02X}", name, value);
        } else {
            // OCW2/3 depending on bit3
            if (value & 8) {
                // OCW3
                switch (value & 7) {
                case 0b010: // read ISR
                    nextReg_ = 0;
                    return;
                case 0b011: // read IRR
                    nextReg_ = 1;
                    return;
                }
                //if (value == 0x6b || value == 0x4a) {
                //    std::println("{}: TODO OCW3 {:X} written?!", name, value);
                //    return;
                //}
            } else {
                // OCW2
                if (value == 0x20) { // non-specific EOI
                    // The highest request level is reset from the IRR when an interrupt is acknowledged.
                    for (int i = 0; i < 8; ++i) {
                        if (isr_ & (1 << i)) {
                            isr_ &= ~(1 << i);
                            return;
                        }
                    }
                    std::println("{}: TODO: non-specific EOI with ISR {:02X}", name, isr_);
                    //THROW_ONCE();
                    return;
                }
                const auto level = value & 7;
                if ((value & 0xf0) == 0x60) {
                    LOG("{}: OCW2 Specific EOI {:02X} to ISR {:02X}, level = {} -> {:02X}", name, value, isr_, level, isr_ & ~(1 << level));
                    isr_ &= ~(1 << level);
                    return;
                }
            }
            throw std::runtime_error { std::format("{}: Unsupported write to OCW{}: {:02X} {:08b}", name, value & 8 ? 3 : 2, value, value) };
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
                assert(companion_);
                std::println("{}: ICW3={:02X}", name, value);
                icw3_ = value;
                icwCnt_ = icw1_ & ICW1_MASK_ICW4 ? 4 : 0;
                if ((isSlave_ && icw3_ > 7) || (!isSlave_ && (icw3_ == 0 || (icw3_ & (icw3_ - 1)))))
                    throw std::runtime_error { std::format("{}: Invalid ICW3: {:02X}", name, value) };
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
            LOG("{}: IMR={:02X} 0b{:08b}", name, value, value);
            imr_ = value;
        }
    }
}

uint8_t i8259a_PIC::pendingMask() const
{
    return irr_ & ~imr_;
}

int i8259a_PIC::getInterrupt()
{
    if (icwCnt_)
        return -1;
    const auto pending = pendingMask();
    if (!pending)
        return -1;

    for (int i = 0; i < 8; ++i) {
        const uint8_t mask = 1 << i;
        // A higher priority interrupt is being serviced
        if (isr_ & mask)
            return -1;
        if (pending & mask) {
            irr_ &= ~mask;
            isr_ |= mask;
            //std::println("PIC: IRQ {}", i);
            if (companion_ && !isSlave_ && (icw3_ & mask))
                return companion_->getInterrupt();
            return i | icw2_;
        }
    }
    throw std::runtime_error { std::format("Internal error in getInterrupt, pending = {}", pending) };
}

void i8259a_PIC::setInterrupt(std::uint8_t line)
{
    line &= 7;
    const uint8_t mask = static_cast<uint8_t>(1 << line);
    irr_ |= mask;

    if (icw1_ & ICW1_MASK_SINGLE)
        return;

    // Cascade mode
    if (isSlave_) {
        companion_->setInterrupt(icw3_);
        return;
    }   
}

void i8259a_PIC::clearInterrupt(std::uint8_t line)
{
    line &= 7;
    irr_ &= ~(1 << line);
}

void i8259a_PIC::addSlave(i8259a_PIC& slave)
{
    assert(companion_ == nullptr);
    assert(slave.companion_ == nullptr);
    companion_ = &slave;
    slave.companion_ = this;
    slave.isSlave_ = true;
}