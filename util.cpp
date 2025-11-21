#include "util.h"
#include <stdexcept>

std::string FormatXString(std::uint64_t value, size_t width, uint8_t shift)
{
    std::string res(width, '0');
    while (width--) {
        res[width] = "0123456789ABCDEF"[value & ((1 << shift) - 1)];
        value >>= shift;
    }
    return res;
}

std::uint8_t DigitValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    else
        return 0xFF;
}

std::vector<std::uint8_t> HexDecode(std::string_view str)
{
    std::vector<std::uint8_t> res;
    bool first = true;
    uint8_t first_digit = 0;

    for (const auto c : str) {
        if (c == ' ' || c == ':' || c == '\r' || c == '\n' || c == '\t')
            continue;

        uint8_t digit = DigitValue(c);

        if (digit == 0xFF)
            throw std::runtime_error { "Invalid hex digit in hex string: " + std::string { str } };

        if (first)
            first_digit = digit;
        else
            res.push_back(first_digit << 4 | digit);
        first = !first;
    }

    // Another option would be to append the digit and right shift the result by 4
    if (!first)
        throw std::runtime_error { "Odd number of nibbles in hex string: " + std::string { str } };

    return res;
}

std::string TrimString(const std::string& s)
{
    size_t beg = 0, end = s.length();
    while (beg < end && std::isspace(static_cast<uint8_t>(s[beg])))
        ++beg;
    while (end > beg && std::isspace(static_cast<uint8_t>(s[end - 1])))
        --end;
    return std::string { s.begin() + beg, s.begin() + end };
}

#include <print>
void HexDump(uint64_t addr, const void* data, size_t size)
{
    constexpr size_t incr = 16;
    auto dat = reinterpret_cast<const std::uint8_t*>(data);

    while (size) {
        const auto here = std::min(incr, size);
        std::print("{:04X} ", addr);
        for (size_t i = 0; i < here; ++i)
            std::print(" {:02x}", dat[i]);
        for (size_t i = here; i < incr; ++i)
            std::print("   ");
        std::print("  ");
        for (size_t i = 0; i < here; ++i)
            std::print("{}", dat[i] >= ' ' && dat[i] < 127 ? static_cast<char>(dat[i]) : '.');
        std::print("\n");
        addr += here;
        dat += here;
        size -= here;
    }
}

std::string HexString(const void* bytes, size_t len)
{
    auto bs = reinterpret_cast<const uint8_t*>(bytes);
    std::string res;
    while (len--)
        res += std::format("{:02x}", *bs++);
    return res;
}

std::string HexString(const std::vector<uint8_t>& bytes)
{
    return HexString(bytes.data(), bytes.size());
}