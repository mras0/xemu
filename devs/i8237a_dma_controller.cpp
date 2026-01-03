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
    impl(SystemBus& bus, uint16_t ioBase, uint16_t pageIoBase, bool wordMode)
        : bus_ { bus }
        , wordMode_ { wordMode }
        , channelCountOffset_ { static_cast<uint8_t>(wordMode ? 4 : 0) }
    {
        assert(pageIoBase == 0x80 || pageIoBase == 0x88);
        assert(wordMode_ == (pageIoBase == 0x88));
        bus.addCycleObserver(*this);
        bus.addIOHandler(ioBase, wordMode_ ? 32 : 16, *this, true);
        bus.addIOHandler(pageIoBase, 8, *this, true);
        reset();
    }

    void reset()
    {
        msbFlipFlop_ = false;
        mask_ = 0xf;
        enabled_ = true;
        std::memset(channels_, 0, sizeof(channels_));
        std::memset(pageReg_, 0, sizeof(pageReg_));
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
    std::uint16_t inU16(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;
    void outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value) override;

    void startGet(uint8_t channel, DMAHandler& handler);

private:
    SystemBus& bus_;
    const bool wordMode_;
    const uint8_t channelCountOffset_;
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
    uint8_t pageReg_[4];

    uint8_t& pageReg(uint16_t port)
    {
        assert((port & 0xfff0) == 0x80);
        switch (port & 7) {
        case 0:
            return pageReg_[0];
        case 1:
            return channels_[2].page;
        case 2:
            return channels_[3].page;
        case 3:
            return channels_[1].page;
        case 4:
            return pageReg_[1];
        case 5:
            return pageReg_[2];
        case 6:
            return pageReg_[3];
        case 7:
            return channels_[0].page;
        }
        throw std::runtime_error { "Not reached" };
    }

    std::string desc() const
    {
        return std::format("DMA{}-{}: ", channelCountOffset_, channelCountOffset_ + 3);
    }

    void internalWrite8(std::uint16_t port, std::uint16_t regNum, std::uint8_t value);
    uint8_t internalRead8(std::uint16_t regNum);
};

std::uint8_t i8237a_DMAController::impl::internalRead8(std::uint16_t regNum)
{
    switch (regNum) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {
        auto value = regNum & 1 ? channels_[regNum >> 1].currentCount : channels_[regNum >> 1].currentAddress;
        if (msbFlipFlop_)
            value >>= 8;
        msbFlipFlop_ = !msbFlipFlop_;
        return static_cast<uint8_t>(value);
    }
    case 0x08:
        // Status register
        // Should be: REQ3|REQ2|REQ1|REQ0|TC3|TC2|TC1|TC0 (TC3-0 are cleared on read)
        //std::println("{}TODO read of status register, just returning 1 (TC0)", desc());
        return 1; // Return TC0 for IBM PC XT BIOS
    default:
        throw std::runtime_error { std::format("{}Unsupported read from register {:02X}", desc(), regNum) };
    }
}

std::uint8_t i8237a_DMAController::impl::inU8(std::uint16_t port, std::uint16_t offset)
{
    if (port >= 0x80 && port <= 0x8F)
        return pageReg(port);

    if (wordMode_) {
        if (!(offset & 1))
            return internalRead8(offset >> 1);
        throw std::runtime_error { std::format("{}Unsupported 8-bit read from register {:02X} (offset {}) -- wordMode!", desc(), port, offset) };
    }
    return internalRead8(offset);
}

std::uint16_t i8237a_DMAController::impl::inU16(std::uint16_t port, std::uint16_t offset)
{
    if (!wordMode_ || (port & 0xfff0) == 0x80)
        return IOHandler::inU16(port, offset);

//    if (!(offset & 1)) {
//        const uint16_t regNum = offset >> 1;
//        if (regNum >= firstActionReg && (value >> 8) == 0) {
//            internalWrite8(port, regNum, static_cast<uint8_t>(value));
//            return;
//        }
//    }

    throw std::runtime_error { std::format("{}Unsupported 16-bit read from register {:02X} (offset {})!", desc(), port, offset) };
}

static constexpr std::uint16_t firstActionReg = 8;

void i8237a_DMAController::impl::internalWrite8(std::uint16_t port, std::uint16_t regNum, std::uint8_t value)
{
    switch (regNum) {
    case 0x08: // Command
        std::println("{}Command {:02X}", desc(), value);
        if (value & ~4)
            throw std::runtime_error { std::format("DMA: Unsupported write value {:02X} (0b{:08b}) for port {:04X} regNum {:02X}", value, value, port, regNum) };
        enabled_ = (value & 4) == 0;
        break;
    case 0x0A: // Mask single channel
        std::println("{}{}masking channel {}", desc(), value & 4 ? "" : "un", value & 3);
        if (value & 4)
            mask_ |= 1 << (value & 3);
        else
            mask_ &= ~(1 << (value & 3));
        break;
    case 0x0B: // Mode write
        std::println("{}Channel {} setting mode to {:02X} {}", desc(), value & MODE_MASK_SEL, value, ModeString(value));
        channels_[value & MODE_MASK_SEL].mode = value & ~MODE_MASK_SEL;
        break;
    case 0x0C: // Clear flip/flop
        msbFlipFlop_ = false;
        break;
    case 0x0D: // Master reset
        std::println("{}Master reset {:02X}", desc(), value);
        reset();
        break;
    default:
        throw std::runtime_error { std::format("{}Unsupported internal 8-bit write value {:02X} (0b{:08b}) for port {:02X} regNum {:02x}", desc(), value, value, port, regNum) };
    }
}

void i8237a_DMAController::impl::outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value)
{
    if (port >= 0x80 && port <= 0x8F) {
        pageReg(port) = value;
        return;
    }

    if (wordMode_) {
        const std::uint16_t regNum = offset >> 1;
        if (!(offset & 1) && regNum >= firstActionReg) {
            internalWrite8(port, regNum, value);
            return;
        }
        throw std::runtime_error { std::format("{}Unsupported 8-bit write value {:02X} (0b{:08b}) for port {:04X} {:04b}  -- wordMode", desc(),value, value, port, offset) };
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
        std::println("{}Channel {} setting {} to {:04X} [{}SB]", desc(), offset >> 1, offset & 1 ? "count" : "address", reg, msbFlipFlop_ ? 'M' : 'L');
        msbFlipFlop_ = !msbFlipFlop_;
        break;
    }
    default:
        internalWrite8(port, offset, value);
    }
}

void i8237a_DMAController::impl::outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value)
{
    if (!wordMode_) {
        IOHandler::outU16(port, offset, value);
        return;
    }

    if (!(offset & 1)) {
        const uint16_t regNum = offset >> 1;
        if (regNum >= firstActionReg && (value >> 8) == 0) {
            internalWrite8(port, regNum, static_cast<uint8_t>(value));
            return;
        }
    }

    throw std::runtime_error { std::format("{}Unsupported 16-bit write value {:04X} (0b{:016b}) for port {:04X} {:04b}", desc(), value, value, port, offset) };
}


void i8237a_DMAController::impl::startGet(uint8_t channel, DMAHandler& handler)
{
    assert(channel < 4);
    auto& ch = channels_[channel];
    std::println("{}Starting get on channel {} address = 0x{:X} count = 0x{:X}", desc(), channel, ch.currentAddress | ch.page << 16, ch.currentCount);

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

i8237a_DMAController::i8237a_DMAController(SystemBus& bus, uint16_t ioBase, uint16_t pageIoBase, bool wordMode)
    : impl_ { std::make_unique<impl>(bus, ioBase, pageIoBase, wordMode) }
{
}
i8237a_DMAController::~i8237a_DMAController() = default;

void i8237a_DMAController::startGet(uint8_t channel, DMAHandler& handler)
{
    impl_->startGet(channel, handler);
}