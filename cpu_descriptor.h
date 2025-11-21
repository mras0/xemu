#ifndef CPU_DESCRIPTOR_H
#define CPU_DESCRIPTOR_H

#include <cstdint>

constexpr uint8_t SD_ACCESS_BIT_DPL = 5;

constexpr uint8_t SD_ACCESS_MASK_A = 1 << 0; // Accessed
constexpr uint8_t SD_ACCESS_MASK_RW = 1 << 1; // R/W
constexpr uint8_t SD_ACCESS_MASK_DC = 1 << 2; // Direction/Conforming
constexpr uint8_t SD_ACCESS_MASK_E = 1 << 3; // Executable
constexpr uint8_t SD_ACCESS_MASK_S = 1 << 4; // 0=System  / 1=Code/data
constexpr uint8_t SD_ACCESS_MASK_DPL = 3 << SD_ACCESS_BIT_DPL;
constexpr uint8_t SD_ACCESS_MASK_P = 1 << 7; // Present

constexpr uint8_t SD_ACCESS_MASK_TYPE = 0xF; // For system segment descriptors
constexpr uint8_t SD_TYPE_TASK16_AVAILABLE = 0x1;
constexpr uint8_t SD_TYPE_LDT = 0x2;
constexpr uint8_t SD_TYPE_TASK16_BUSY = 0x3;
constexpr uint8_t SD_TYPE_TASK32_AVAILABLE = 0x9;
constexpr uint8_t SD_TYPE_TASK32_BUSY = 0xB;
constexpr uint8_t SD_TYPE_CALL32 = 0xC; // Call-gate descriptor

constexpr uint8_t SD_FLAGS_MASK_L = 1 << 1; // Long-mode code flag
constexpr uint8_t SD_FLAGS_MASK_DB = 1 << 2; // DB: Size (0 = 16-bit, 1 = 32-bit)
constexpr uint8_t SD_FLAGS_MASK_G = 1 << 3; // G: Granularity of limit (0 = bytes, 1 = 4KB blocks)

constexpr uint16_t DESC_MASK_DPL = 3;
constexpr uint16_t DESC_MASK_LOCAL = 4;

struct DescriptorTable {
    uint16_t limit;
    uint64_t base;
};

struct SegmentDescriptor {
    union {
        uint64_t raw;
        struct {
            uint16_t offsetLow;
            uint16_t selector;
            uint16_t paramCount : 4;
            uint16_t : 12;
            uint16_t offsetHigh;

            uint32_t offset() const
            {
                return offsetLow | offsetHigh << 16;
            }
        } call32;
    };

    uint32_t limit;
    uint64_t base;
    uint8_t flags;
    uint8_t access;

    uint8_t dpl() const
    {
        return (access & SD_ACCESS_MASK_DPL) >> SD_ACCESS_BIT_DPL;
    }

    bool present() const
    {
        return (access & SD_ACCESS_MASK_P) != 0;
    }

    bool isCodeSegment() const
    {
        return (access & (SD_ACCESS_MASK_S | SD_ACCESS_MASK_E)) == (SD_ACCESS_MASK_S | SD_ACCESS_MASK_E);
    }

    void setRealModeCode(uint16_t value)
    {
        *this = fromU64(toRaw(0xffff, static_cast<uint64_t>(value) << 4, SD_ACCESS_MASK_S | SD_ACCESS_MASK_E | SD_ACCESS_MASK_RW | SD_ACCESS_MASK_P, 0));
    }

    void setRealModeData(uint16_t value)
    {
        *this = fromU64(toRaw(0xffff, static_cast<uint64_t>(value) << 4, SD_ACCESS_MASK_S | SD_ACCESS_MASK_RW | SD_ACCESS_MASK_P, 0));
    }

    static constexpr uint64_t toRaw(uint32_t limit, uint64_t base, uint8_t access, uint8_t flags)
    {
        return (limit & 0xffff) | static_cast<uint64_t>((limit >> 20) & 0xf) << 48 | (base & 0xFFFFFF) << 16 | (base >> 24) << 56 | static_cast<uint64_t>(access) << 40 | static_cast<uint64_t>(flags) << 52;
    }

    static constexpr SegmentDescriptor fromU64(std::uint64_t d)
    {
        SegmentDescriptor desc;
        desc.raw = d;
        desc.limit = (d & 0xffff) | ((d >> 48) & 0xf) << 16;
        desc.base = ((d >> 16) & 0xffffff) | ((d >> 56) << 24);
        desc.access = static_cast<uint8_t>(d >> 40);
        desc.flags = static_cast<uint8_t>((d >> 52) & 0xf);

        if (desc.flags & SD_FLAGS_MASK_G)
            desc.limit = desc.limit << 12 | 0xfff; // Limit is in 4K blocks
        return desc;
    }
};

#include <format>
template <>
struct std::formatter<SegmentDescriptor> : std::formatter<const char*> {
    std::format_context::iterator format(const SegmentDescriptor& sd, std::format_context& ctx) const;
};

#endif
