#include "cpu_descriptor.h"
#include <string>

std::format_context::iterator std::formatter<SegmentDescriptor>::format(const SegmentDescriptor& sd, std::format_context& ctx) const
{
    auto str = std::format("SegmentDescriptor({:04x}`{:04x}`{:04x}`{:04x} P={} DPL={} S={}", (sd.raw >> 48) & 0xffff, (sd.raw >> 32) & 0xffff, (sd.raw >> 16) & 0xffff, sd.raw & 0xffff, !!(sd.access & SD_ACCESS_MASK_P), sd.dpl(), !!(sd.access & SD_ACCESS_MASK_S));
    if (sd.access & SD_ACCESS_MASK_S) {
        // CODE/DATA
        if (sd.flags & SD_FLAGS_MASK_L) {
            str += " 64-bit code";
        } else {
            if (sd.flags & SD_FLAGS_MASK_DB)
                str += " 32-bit";
            else
                str += " 16-bit";
            if (sd.access & SD_ACCESS_MASK_E)
                str += " code";
            else
                str += " data";
        }
FormatDesc:
        str += std::format(" access=0x{:X} flags=0x{:X} base=0x{:X} limit=0x{:X}", sd.access, sd.flags, sd.base, sd.limit);
    } else {
        const auto type = sd.access & SD_ACCESS_MASK_TYPE;
        str += std::format(" type=0x{:X}", type);
        switch (type) {
        case SD_TYPE_CALL32:
            str += std::format(" 32-bit call gate, {:04X}:{:08X} param count=0x{:X}", sd.call32.selector, sd.call32.offset(), sd.call32.paramCount);
            break;
        case SD_TYPE_TASK32_AVAILABLE:
        case SD_TYPE_TASK32_BUSY:
            str += std::format(" 32-bit TSS {}", type == SD_TYPE_TASK32_AVAILABLE ? "Available" : "Busy");
            goto FormatDesc;
        }
    }
    str += ')';
    return std::formatter<const char*>::format(str.c_str(), ctx);
}