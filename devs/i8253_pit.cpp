#include "i8253_pit.h"
#include <format>
#include <print>
#include <cstring>

static constexpr uint8_t accessShift = 4;
static constexpr uint8_t accessMask = 3 << accessShift;
static constexpr uint8_t modeShift = 1;
static constexpr uint8_t modeMask = 7 << modeShift;
static constexpr uint8_t bcdMask = 1 << 0;

i8253_PIT::i8253_PIT(SystemBus& bus, CallbackType cb)
    : bus_ { bus }
    , cb_ { cb }
{
    bus.addCycleObserver(*this);
    bus.addIOHandler(0x40, 4, *this, true);
    reset();
}

void i8253_PIT::reset()
{
    cycles_ = 0;
    std::memset(&channel_, 0, sizeof(channel_));
}

void i8253_PIT::runCycles(std::uint64_t numCycles)
{
    // Rate is 1/12th of the system bus frequency
    cycles_ += numCycles;
    auto numCounts = cycles_ / 12;
    cycles_ %= 12;
    while (numCounts--) {
        for (int i = 0; i < 3; ++i) {
            if (channel_[i].clock(bus_) && i == 0)
                cb_();
        }
    }
}

std::uint64_t i8253_PIT::nextAction()
{
    auto& ch = channel_[0];
    if (ch.active) {
        if (ch.reload)
            return 12;
        return ch.counter * 12;
    }
    return UINT64_MAX;
}

std::uint8_t i8253_PIT::inU8(uint16_t port, uint16_t offset)
{
    if (offset < 3) {
        auto val = channel_[offset].latch;
        channel_[offset].latch >>= 8;
        return static_cast<uint8_t>(val);
    }
    return IOHandler::inU8(port, offset);
}

void i8253_PIT::outU8(uint16_t port, uint16_t offset, std::uint8_t value)
{
    if (offset == 3) {
        const auto ch = value >> 6;
        if (ch == 3)
            throw std::runtime_error { std::format("PIT: Read-back not supported 0x{:02X}", value) };
        if (((value & accessMask) >> accessShift) == 0) {
            channel_[ch].latch = channel_[ch].counter;
            //std::println("PIT: Latching channel {} value=0x{:04X}", ch, channel_[ch].latch);
            return;
        }

        std::println("PIT channel {} access mode={:02b} operating mode={:03b} bcd={}", ch, (value & accessMask) >> accessShift, (value & modeMask) >> modeShift, value & bcdMask);
        const auto mode = (value & modeMask) >> modeShift;
        if (mode != 0 && mode != 2 && mode != 3)
            throw std::runtime_error { std::format("PIT: TODO mode not supported value=0x{:02X}", value) };

        if (value & bcdMask)
            throw std::runtime_error { std::format("PIT: BCD not supported 0x{:02X}", value) };
        channel_[ch].control = value & 0x3f;
        channel_[ch].msb = false;
    } else {
        auto& ch = channel_[offset];
        bool loaded = true;
        switch ((ch.control & accessMask) >> accessShift) {
        // case 0b00: // Latch
        case 0b01: // Low byte only
            ch.initialCount = value;
            break;
        case 0b10: // High byte only
            ch.initialCount = value << 8;
            break;
        case 0b11: // Lobyte/hibyte
            if (ch.msb) {
                ch.initialCount = (ch.initialCount & 0xff) | value << 8;
            } else {
                ch.initialCount = (ch.initialCount & 0xff00) | value;
                loaded = false;
            }
            ch.msb = !ch.msb;
            break;
        }
        if (loaded) {
            std::println("PIT: Channel {}: Reload=0x{:04X}", port & 3, ch.initialCount);
            ch.active = true;
            ch.reload = true;
            bus_.recalcNextAction();
        }
    }
}

bool i8253_PIT::Channel::clock(SystemBus& bus)
{
    if (!active)
        return false;

    if (reload) {
        counter = initialCount;
        reload = false;
        bus.recalcNextAction();
    }

    // TODO: Actually handle the different modes...
    const auto mode = (control & modeMask) >> modeShift;

    if (!counter) {
        counter = 0xffff;
    } else {
        counter--;
        if (counter == 0) {
            if (mode == 0)
                active = false;
            else
                reload = true;
            return true;
        }
    }
    return false;
}
