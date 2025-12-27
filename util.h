#ifndef UTIL_H
#define UTIL_H

#include <cstdint>
#include <string>
#include <vector>
#include <concepts>
#include <string_view>
#include <cassert>
#include <utility>

#define THROW_ONCE() do { static bool passed_before_; if (!passed_before_) { passed_before_ = true; throw std::runtime_error{"FORCE BREAK from " + std::string(__func__) + ":" + std::to_string(__LINE__)}; } } while (0)
#define THROW_FLIPFLOP() do { static bool flipflop_; if (!std::exchange(flipflop_, !flipflop_)) throw std::runtime_error("FORCED FLIPFLOP BREAK from " + std::string(__func__) + ":" + std::to_string(__LINE__)); } while (0);

std::string FormatXString(std::uint64_t value, size_t width, uint8_t shift);

template <std::integral I>
std::string BinString(I value, size_t width = 8 * sizeof(I))
{
    return FormatXString(value, width, 1);
}

template <std::integral I>
std::string HexString(I value, size_t width = 2 * sizeof(I))
{
    return FormatXString(value, width, 4);
}

std::string HexString(const void* bytes, size_t len);
std::string HexString(const std::vector<uint8_t>& bytes);

std::vector<std::uint8_t> HexDecode(std::string_view str);
std::string TrimString(const std::string& s);

std::uint8_t DigitValue(char ch); // 0xFF = invalid number

void HexDump(uint64_t addr, const void* data, size_t size);

constexpr std::uint64_t SignExtend(std::uint64_t val, std::uint8_t valSize)
{
    std::int64_t r = val;
    switch (valSize) {
    case 1:
        r = static_cast<int8_t>(val & 0xff);
        break;
    case 2:
        r = static_cast<int16_t>(val & 0xffff);
        break;
    case 4:
        r = static_cast<int32_t>(val & 0xffffffff);
        break;
    case 8:
        break;
    default:
        assert(false);
    }
    return static_cast<uint64_t>(r);
}

inline uint16_t GetU16(const uint8_t* src)
{
    return src[0] | src[1] << 8;
}

inline uint32_t GetU32(const uint8_t* src)
{
    return src[0] | src[1] << 8 | src[2] << 16 | src[3] << 24;
}

inline void PutU16(uint8_t* dest, uint16_t value)
{
    dest[0] = static_cast<uint8_t>(value);
    dest[1] = static_cast<uint8_t>(value >> 8);
}

inline void PutU32(uint8_t* dest, uint32_t value)
{
    dest[0] = static_cast<uint8_t>(value);
    dest[1] = static_cast<uint8_t>(value >> 8);
    dest[2] = static_cast<uint8_t>(value >> 16);
    dest[3] = static_cast<uint8_t>(value >> 24);
}

#endif
