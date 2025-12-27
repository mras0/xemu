#ifndef ADDRESS_H
#define ADDRESS_H

#include <cstdint>
#include <cassert>

class Address {
public:
    constexpr Address()
        : segment_ { 0 }
        , offset_ { UINT64_MAX }
        , offsetSize_ { 8 }
    {
    }

    constexpr explicit Address(std::uint16_t segment, std::uint64_t offset, std::uint8_t offsetSize)
        : segment_ { segment }
        , offset_ { offset }
        , offsetSize_ { offsetSize }
    {
        assert(offsetSize == 2 || offsetSize == 4);
    }

    constexpr std::uint16_t segment() const { return segment_; }
    constexpr std::uint64_t offset() const { return offset_; }
    constexpr std::uint8_t offsetSize() const { return offsetSize_; }

    constexpr bool operator==(const Address& rhs) const = default;

    Address operator+(int64_t incr) const
    {
        auto res = *this;
        return res += incr;
    }

    Address& operator+=(int64_t incr)
    {
        offset_ += incr;
        if (offsetSize_ < 8)
            offset_ &= (std::uint64_t(1) << 8 * offsetSize_) - 1;
        return *this;
    }

private:
    std::uint16_t segment_;
    std::uint64_t offset_;
    std::uint8_t offsetSize_;
};

#include <format>

template<>
struct std::formatter<Address> : std::formatter<const char*> {
    auto format(Address a, std::format_context& ctx) const
    {
        char buffer[32];
        const auto seg = a.segment();
        const auto ofs = a.offset();
        switch (a.offsetSize()) {
        default:
            *std::format_to_n(buffer, sizeof(buffer) - 1, "{:04X}:{:0{}X}", seg, ofs, a.offsetSize() * 2).out = '\0';
            break;
        case 8:
            *std::format_to_n(buffer, sizeof(buffer) - 1, "{:04X}:{:08X}`{:08X}", seg, ofs >> 32, ofs & 0xffffffff).out = '\0';
            break;
        }
        return std::formatter<const char*>::format(buffer, ctx);
    }
};

#endif
