#include "util.h"
#include <stdexcept>
#include <chrono>

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

// Date/Time

static constexpr int64_t seconds_per_day = 24 * 60 * 60;
static constexpr int64_t epoch_offset = -2440588 * seconds_per_day; // 2440588 is JDN for 1970-01-01

static constexpr bool IsLeapYear(uint32_t year)
{
    return year % 4 == 0 && (year % 400 == 0 || year % 100 != 0);
}

static constexpr uint32_t MonthLength(uint32_t year, uint32_t month)
{
    if (month == 2)
        return 28 + IsLeapYear(year);
    return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

static constexpr int JulianDay(int Y, int M, int D)
{
    const int A = (M - 14) / 12;
    return 1461 * (Y + 4800 + A) / 4 + 367 * (M - 2 - 12 * A) / 12 - 3 * ((Y + 4900 + A) / 100) / 4 + D - 32075;
}

int64_t SecondsSinceEpoch(uint32_t Y, uint32_t M, uint32_t D, uint32_t h, uint32_t m, uint32_t s)
{
    if (M < 1 || M > 12 || D < 1 || D > MonthLength(Y, M) || h > 23 || m > 59 || s > 59)
        throw std::runtime_error { "Invalid time stamp" };

    // N.B. the julian day starts at noon (12:00)
    return epoch_offset + JulianDay(Y, M, D) * seconds_per_day + (h * 60 + m) * 60 + s;
}

int64_t SecondsSinceEpoch(const DateTime& dt)
{
    return SecondsSinceEpoch(dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds);
}

DateTime DateTimeFromEpochTime(int64_t t)
{
    constexpr auto y = 4716;
    constexpr auto j = 1401;
    constexpr auto m = 2;
    constexpr auto n = 12;
    constexpr auto r = 4;
    constexpr auto p = 1461;
    constexpr auto v = 3;
    constexpr auto u = 5;
    constexpr auto s = 153;
    constexpr auto w = 2;
    constexpr auto B = 274277;
    constexpr auto C = -38;

    const auto J = (t - epoch_offset) / seconds_per_day;
    const auto f = J + j + (((4 * J + B) / 146097) * 3) / 4 + C;
    const auto e = r * f + v;
    const auto g = (e % p) / r;
    const auto h = u * g + w;
    const uint32_t D = static_cast<uint32_t>((h % s) / u + 1);
    const uint32_t M = static_cast<uint32_t>((h / s + m) % n + 1);
    const uint32_t Y = static_cast<uint32_t>(e / p - y + (n + m - M) / n);

    t %= seconds_per_day;
    const uint32_t hrs = static_cast<uint32_t>(t / 3600);
    t %= 3600;
    const uint32_t min = static_cast<uint32_t>(t / 60);
    const uint32_t sec = static_cast<uint32_t>(t % 60);

    return DateTime { Y, M, D, hrs, min, sec };
}

static auto SysTime()
{
    return std::chrono::system_clock::now();
}

static auto LocalTime()
{
    auto tz = std::chrono::current_zone();
    if (!tz)
        throw std::runtime_error { "Local time is not available" };
    return tz->to_local(SysTime());
}

int64_t CurrentEpochTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(SysTime().time_since_epoch()).count();
}

int64_t CurrentEpochLocalTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(LocalTime().time_since_epoch()).count();
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