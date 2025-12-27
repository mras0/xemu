#include "exehdr.h"
#include "util.h"
#include <print>

namespace exehdr {

static_assert(sizeof(DosExeHeader) == 0x1c);

#define DOS_HEADER_FIELDS(X) \
    X(magic                     , "Magic number") \
    X(cblp                      , "Bytes on last page of file") \
    X(cp                        , "Pages in file") \
    X(crlc                      , "Relocations") \
    X(cparhdr                   , "Size of header in paragraphs") \
    X(minalloc                  , "Minimum extra paragraphs needed") \
    X(maxalloc                  , "Maximum extra paragraphs needed") \
    X(ss                        , "Initial (relative) SS value") \
    X(sp                        , "Initial SP value") \
    X(csum                      , "Checksum") \
    X(ip                        , "Initial IP value") \
    X(cs                        , "Initial (relative) CS value") \
    X(lfarlc                    , "File address of relocation table") \
    X(ovno                      , "Overlay number")

#define VXD_HEADER_FIELDS(X) \
    X(magic                   , "Magic number") \
    X(border                  , "The byte ordering for the VXD") \
    X(worder                  , "The word ordering for the VXD") \
    X(level                   , "The EXE format level for now = 0") \
    X(cpu                     , "The CPU type") \
    X(os                      , "The OS type") \
    X(ver                     , "Module version") \
    X(mflags                  , "Module flags") \
    X(mpages                  , "Module # pages") \
    X(startobj                , "Object # for instruction pointer") \
    X(eip                     , "Extended instruction pointer") \
    X(stackobj                , "Object # for stack pointer") \
    X(esp                     , "Extended stack pointer") \
    X(pagesize                , "VXD page size") \
    X(lastpagesize            , "Last page size in VXD") \
    X(fixupsize               , "Fixup section size") \
    X(fixupsum                , "Fixup section checksum") \
    X(ldrsize                 , "Loader section size") \
    X(ldrsum                  , "Loader section checksum") \
    X(objtab                  , "Object table offset") \
    X(objcnt                  , "Number of objects in module") \
    X(objmap                  , "Object page map offset") \
    X(itermap                 , "Object iterated data map offset") \
    X(rsrctab                 , "Offset of Resource Table") \
    X(rsrccnt                 , "Number of resource entries") \
    X(restab                  , "Offset of resident name table") \
    X(enttab                  , "Offset of Entry Table") \
    X(dirtab                  , "Offset of Module Directive Table") \
    X(dircnt                  , "Number of module directives") \
    X(fpagetab                , "Offset of Fixup Page Table") \
    X(frectab                 , "Offset of Fixup Record Table") \
    X(impmod                  , "Offset of Import Module Name Table") \
    X(impmodcnt               , "Number of entries in Import Module Name Table") \
    X(impproc                 , "Offset of Import Procedure Name Table") \
    X(pagesum                 , "Offset of Per-Page Checksum Table") \
    X(datapage                , "Offset of Enumerated Data Pages") \
    X(preload                 , "Number of preload pages") \
    X(nrestab                 , "Offset of Non-resident Names Table") \
    X(cbnrestab               , "Size of Non-resident Name Table") \
    X(nressum                 , "Non-resident Name Table Checksum") \
    X(autodata                , "Object # for automatic data object") \
    X(debuginfo               , "Offset of the debugging information") \
    X(debuglen                , "The length of the debugging info. in bytes") \
    X(instpreload             , "Number of instance pages in preload section of VXD file") \
    X(instdemand              , "Number of instance pages in demand load section of VXD file") \
    X(heapsize                , "Size of heap - for 16-bit apps") \
    /*X(res3                    , "Reserved words") */\
    X(winresoff               , "Resource offset") \
    X(winreslen               , "Resource length") \
    X(devid                   , "Device ID for VxD") \
    X(ddkver                  , "DDK version for VxD") \


void PrintExeHeader(std::FILE* fp, const uint8_t* data, size_t size)
{
    if (size < DosExeHeader_lfanew_offset + 4) {
        std::println(fp, "File is too small");
        return;
    }

    const DosExeHeader& dosHdr = *reinterpret_cast<const DosExeHeader*>(data);

    if (dosHdr.magic != IMAGE_DOS_SIGNATURE) {
        std::println(fp, "Unknonwn signature {:04X} '{:c}{:c}'", dosHdr.magic, dosHdr.magic & 0xff, dosHdr.magic >> 8);
        return;
    }
#define PRINT_FIELD(field, desc) std::println(fp, "{:32s} 0x{:0{}X}", desc, dosHdr.field, 2 * sizeof(dosHdr.field));
    DOS_HEADER_FIELDS(PRINT_FIELD)
#undef PRINT_FIELD

    if (dosHdr.crlc) {
        if (dosHdr.lfarlc + static_cast<size_t>(dosHdr.crlc) * 4 > size) {
            std::println(fp, "Relocation table out of range.");
            return;
        }
        std::println(fp, "Relocations:");
        for (int i = 0; i < dosHdr.crlc; ++i) {
            const auto reloc = GetU32(data + dosHdr.lfarlc + i * 4);
            std::println(fp, "  {:04X}:{:04X}", reloc >> 16, reloc & 0xffff);
        }
    }
    const auto lfanew = GetU32(data + DosExeHeader_lfanew_offset);
    if (lfanew + 4 < size) {
        const auto sig = GetU32(data + lfanew);
        if (sig == IMAGE_NT_SIGNATURE) {
            std::println(fp, "NT PE header!");
        } else if ((sig & 0xffff) == W3_SIGNATURE) {
            // TODO: Length check
            // https://github.com/joncampbell123/doslib/blob/master/tool/w3extract.pl
            // LE executables: https://www.ecsdump.net/?page_id=1151
            const uint16_t numDirElements = GetU16(data + lfanew + 4);
            std::println(fp, "W3 pack! Windows version {}.{}. {} directory elements", sig >> 24, (sig >> 16) & 0xff, numDirElements);
            uint32_t offset = lfanew + 16;
            for (int i = 0; i < numDirElements; ++i) {
                std::println("{:8s} {:08X} {:08X}", std::string(&data[offset], &data[offset+8]), GetU32(data + offset + 8), GetU32(data + offset + 12));
                offset += 16;
            }
        } else if (sig == IMAGE_VXD_SIGNATURE && lfanew + sizeof(VxDHeader) < size) { // LE followed by two zero bytes for little endian
            const auto& vxdHdr = *reinterpret_cast<const VxDHeader*>(data + lfanew);
            std::println(fp, "VxD / LE!");
#define PRINT_FIELD(field, desc) std::println(fp, "{:02X} {:32s} 0x{:0{}X}", offsetof(VxDHeader, field), desc, vxdHdr.field, 2 * sizeof(vxdHdr.field));
            VXD_HEADER_FIELDS(PRINT_FIELD)
#undef PRINT_FIELD

            for (uint32_t i = 0; i < vxdHdr.objcnt; ++i) {
                const auto offset = lfanew + vxdHdr.objtab + sizeof(VxdObjectHeader) * i;
                const auto& objHdr = *reinterpret_cast<const VxdObjectHeader*>(data + offset);
                std::println(fp, "Segment {}", i);
                std::println(fp, "  virtualSegmentSize    {:08X}", objHdr.virtualSegmentSize);
                std::println(fp, "  relocationBaseAddress {:08X}", objHdr.relocationBaseAddress);
                std::println(fp, "  flags                 {:08X} 0b{:b}", objHdr.flags, objHdr.flags);
                std::println(fp, "  pageMapIndex          {:08X}", objHdr.pageMapIndex);
                std::println(fp, "  pageMapEntries        {:08X}", objHdr.pageMapEntries);
            }
            #if 0
            for (uint32_t i = 0; i < vxdHdr.mpages; ++i) {
                struct VxdPageInfo {
                    std::uint16_t highPageNum;
                    std::uint8_t lowPageNum;
                    std::uint8_t flags;
                };
                const auto offset = lfanew + vxdHdr.objmap + 4 * i;
                const auto& pageInfo = *reinterpret_cast<const VxdPageInfo*>(data + offset);
                std::println(fp, "Page {:02X} {:04X} {:02X} {:02X}", i, pageInfo.highPageNum, pageInfo.lowPageNum, pageInfo.flags);
            }
            #endif
        }
    }
}


} // namespace exehdr
