#include "i8237a_dma_controller.h"
#include <cassert>
#include <print>
#include <format>
#include <cstring>

namespace {

enum : uint8_t {
    MODE_BIT_SEL0,
    MODE_BIT_SEL1,
    MODE_BIT_TRA0,
    MODE_BIT_TRA1,
    MODE_BIT_AUTO,
    MODE_BIT_DOWN,
    MODE_BIT_MOD0,
    MODE_BIT_MOD1,
};

enum : uint8_t {
    MODE_MASK_SEL = 3,
    MODE_MASK_TRA = 3 << MODE_BIT_TRA0,
    MODE_MASK_AUTO = 1 << MODE_BIT_AUTO,
    MODE_MASK_DOWN = 1 << MODE_BIT_DOWN,
    MODE_MASK_MOD = 3 << MODE_BIT_MOD0,
};

enum : uint8_t {
    TRA_SELF_TEST,
    TRA_WRITE,
    TRA_READ,
    TRA_INVALID
};

enum : uint8_t {
    MODE_ON_DEMAND,
    MODE_SINGLE,
    MODE_BLOCK,
    MODE_CASCADE,
};

std::string ModeString(uint8_t mode)
{
    const char* tra[4] = { "selftest", "write", "read", "invalid" }; // write == peripheral -> memory
    const char* mod[4] = { "on-demand", "single", "block", "cascade" };
    return std::format("{} {} {}{}", tra[(mode & MODE_MASK_TRA) >> MODE_BIT_TRA0], mod[(mode & MODE_MASK_MOD) >> MODE_BIT_MOD0], mode & MODE_MASK_AUTO ? "auto " : "", mode & MODE_MASK_DOWN ? "down" : "up");
}

} // unnamed namespace

class i8237a_DMAController::impl : public IOHandler, public CycleObserver {
public:
    impl(SystemBus& bus, uint16_t ioBase, uint16_t pageIoBase)
        : bus_ { bus }
    {
        assert(pageIoBase == 0x81 || pageIoBase == 0x89);
        bus.addCycleObserver(*this);
        bus.addIOHandler(ioBase, 16, *this, true);
        bus.addIOHandler(pageIoBase, 3, *this, true);
        bus.addIOHandler(pageIoBase|7, 1, *this, true);
        reset();
    }

    void reset()
    {
        msbFlipFlop_ = false;
        mask_ = 0xf;
        enabled_ = false;
        std::memset(channels_, 0, sizeof(channels_));
    }

    void runCycles([[maybe_unused]] std::uint64_t cycles) override
    {
        // Fake activity for the sake of IBM XT BIOS (uses it for delay...)
        auto& ch0 = channels_[0];
        if (enabled_ && ch0.mode == 0x58)
            ch0.currentAddress--;
    }

    std::uint64_t nextAction() override
    {
        return UINT64_MAX;
    }

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;

    void startGet(uint8_t channel, DMAHandler& handler);

private:
    SystemBus& bus_;
    bool msbFlipFlop_;
    uint8_t mask_;
    bool enabled_;

    struct Channel {
        uint16_t baseAddress;
        uint16_t currentAddress;
        uint16_t baseCount;
        uint16_t currentCount;
        uint8_t page;
        uint8_t mode;
    } channels_[4];
};

std::uint8_t i8237a_DMAController::impl::inU8(std::uint16_t port, std::uint16_t offset)
{
    switch (offset) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {
        auto value = offset & 1 ? channels_[offset >> 1].currentCount : channels_[offset >> 1].currentAddress;
        if (msbFlipFlop_)
            value >>= 8;
        msbFlipFlop_ = !msbFlipFlop_;
        return static_cast<uint8_t>(value);
    }
    case 0x08:
        //Status register
        //Should be: REQ3|REQ2|REQ1|REQ0|TC3|TC2|TC1|TC0 (TC3-0 are cleared on read)
        std::println("DMA: TODO read of status register, just returning 1 (TC0)");
        return 1; // Return TC0 for IBM PC XT BIOS
    default:
        throw std::runtime_error { std::format("DMA: Unsupported read from register {:02X} (offset {})", port, offset) };
    }
}

void i8237a_DMAController::impl::outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value)
{
    if (port & 0x80) {
        const auto idx = (port & 7) == 7 ? 0 : (port & 3) == 3 ? 1
            : (port & 3) == 1                                  ? 2
                                                               : 3;
        std::println("DMA: Channel {} setting page register to {:02x}",   idx, value);
        channels_[idx].page = value;
        return;
    }

    switch (offset) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {
        // TODO: Block if channel is active..
        auto& reg = offset & 1 ? channels_[offset >> 1].baseCount : channels_[offset >> 1].baseAddress;
        auto& reg2 = offset & 1 ? channels_[offset >> 1].currentCount : channels_[offset >> 1].currentAddress;
        if (msbFlipFlop_) {
            reg2 = reg = (reg & 0xff) | value << 8;
        } else {
            reg2 = reg = (reg & 0xff00) | value;
        }
        std::println("DMA: Channel {} setting {} to {:04X} [{}SB]", offset >> 1, offset & 1 ? "count" : "address", reg, msbFlipFlop_ ? 'M' : 'L');
        msbFlipFlop_ = !msbFlipFlop_;
        break;
    }
    case 0x08: // Command
        std::println("DMA: Command {:02X}", value);
        if (value & ~4)
            throw std::runtime_error { std::format("DMA: Unsupported write value {:02X} (0b{:08b}) for port {:04X} {:04b}", value, value, port, offset) };
        enabled_ = (value & 4) == 0;
        break;
    case 0x0A: // Mask single channel
        std::println("DMA: {}masking channel {}", value & 4 ? "": "un", value & 3);
        if (value & 4)
            mask_ |= 1 << (value & 3);
        else
            mask_ &= ~(1 << (value & 3));
        break;
    case 0x0B: // Mode write
        std::println("DMA: Channel {} setting mode to {:02X} {}", value & MODE_MASK_SEL, value, ModeString(value));
        channels_[value & MODE_MASK_SEL].mode = value & ~MODE_MASK_SEL;
        break;
    case 0x0C: // Clear flip/flop
        msbFlipFlop_ = false;
        break;
    case 0x0D: // Master reset
        std::println("DMA: Master reset {:02X}", value);
        reset();
        break;
    default:
        throw std::runtime_error { std::format("DMA: Unsupported write value {:02X} (0b{:08b}) for port {:04X} {:04b}", value, value, port, offset) };
    }
}

void i8237a_DMAController::impl::startGet(uint8_t channel, DMAHandler& handler)
{
    assert(channel < 4);
    auto& ch = channels_[channel];
    std::println("DMA: Starting get on channel {} address = 0x{:X} count = 0x{:X}", channel, ch.currentAddress | ch.page << 16, ch.currentCount);

    if (!enabled_)
        throw std::runtime_error { std::format("DMA: Unsupported write (get) - channel {}, DMA controller disabled", channel) };

    if (mask_ & (1 << channel))
        throw std::runtime_error { std::format("DMA: Unsupported write (get) - channel {} is currently masked", channel) };

    if ((ch.mode & ~MODE_MASK_AUTO) != (MODE_SINGLE << MODE_BIT_MOD0 | TRA_WRITE << MODE_BIT_TRA0))
        throw std::runtime_error { std::format("DMA: Unsupported write (get) mode", ModeString(ch.mode)) };

    do {
        bus_.writeU8(ch.currentAddress | ch.page << 16, handler.dmaGetU8());
        ch.currentAddress += 1;
        --ch.currentCount;
    } while (ch.currentCount != 0xFFFF);

    if (ch.mode & MODE_MASK_AUTO) {
        ch.currentAddress = ch.baseAddress;
        ch.currentCount = ch.baseCount;
    } else {
        mask_ |= 1 << channel;
    }

    handler.dmaDone();
}

i8237a_DMAController::i8237a_DMAController(SystemBus& bus, uint16_t ioBase, uint16_t pageIoBase)
    : impl_ { std::make_unique<impl>(bus, ioBase, pageIoBase) }
{
}
i8237a_DMAController::~i8237a_DMAController() = default;

void i8237a_DMAController::startGet(uint8_t channel, DMAHandler& handler)
{
    impl_->startGet(channel, handler);
}