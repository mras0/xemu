#ifndef EXEHDR_H
#define EXEHDR_H

#include <cstdint>
#include <cstdio>

namespace exehdr {

constexpr uint16_t IMAGE_DOS_SIGNATURE    = 0x5A4D;      // MZ
constexpr uint16_t IMAGE_OS2_SIGNATURE    = 0x454E;      // NE
constexpr uint16_t IMAGE_OS2_SIGNATURE_LE = 0x454C;      // LE
constexpr uint16_t IMAGE_VXD_SIGNATURE    = 0x454C;      // LE
constexpr uint32_t IMAGE_NT_SIGNATURE     = 0x00004550;  // PE00

constexpr uint16_t W3_SIGNATURE = 'W' | '3' << 8;

struct DosExeHeader {            // DOS .EXE header
    std::uint16_t magic;         // Magic number
    std::uint16_t cblp;          // Bytes on last page of file
    std::uint16_t cp;            // Pages in file
    std::uint16_t crlc;          // Relocations
    std::uint16_t cparhdr;       // Size of header in paragraphs
    std::uint16_t minalloc;      // Minimum extra paragraphs needed
    std::uint16_t maxalloc;      // Maximum extra paragraphs needed
    std::uint16_t ss;            // Initial (relative) SS value
    std::uint16_t sp;            // Initial SP value
    std::uint16_t csum;          // Checksum
    std::uint16_t ip;            // Initial IP value
    std::uint16_t cs;            // Initial (relative) CS value
    std::uint16_t lfarlc;        // File address of relocation table
    std::uint16_t ovno;          // Overlay number
    // The below fields (except for one word are only for PE executables)
    // std::uint16_t   e_res[4];                    // Reserved words
    // std::uint16_t   e_oemid;                     // OEM identifier (for e_oeminfo)
    // std::uint16_t   e_oeminfo;                   // OEM information; e_oemid specific
    // std::uint16_t   e_res2[10];                  // Reserved words
    // std::uint32_t  e_lfanew;                    // File address of new exe header
};

struct VxDHeader {                          // Windows VXD header
    std::uint16_t  magic;                   // Magic number
    std::uint8_t   border;                  // The byte ordering for the VXD
    std::uint8_t   worder;                  // The word ordering for the VXD
    std::uint32_t  level;                   // The EXE format level for now = 0
    std::uint16_t  cpu;                     // The CPU type
    std::uint16_t  os;                      // The OS type
    std::uint32_t  ver;                     // Module version
    std::uint32_t  mflags;                  // Module flags
    std::uint32_t  mpages;                  // Module # pages
    std::uint32_t  startobj;                // Object # for instruction pointer
    std::uint32_t  eip;                     // Extended instruction pointer
    std::uint32_t  stackobj;                // Object # for stack pointer
    std::uint32_t  esp;                     // Extended stack pointer
    std::uint32_t  pagesize;                // VXD page size
    std::uint32_t  lastpagesize;            // Last page size in VXD
    std::uint32_t  fixupsize;               // Fixup section size
    std::uint32_t  fixupsum;                // Fixup section checksum
    std::uint32_t  ldrsize;                 // Loader section size
    std::uint32_t  ldrsum;                  // Loader section checksum
    std::uint32_t  objtab;                  // Object table offset
    std::uint32_t  objcnt;                  // Number of objects in module
    std::uint32_t  objmap;                  // Object page map offset
    std::uint32_t  itermap;                 // Object iterated data map offset
    std::uint32_t  rsrctab;                 // Offset of Resource Table
    std::uint32_t  rsrccnt;                 // Number of resource entries
    std::uint32_t  restab;                  // Offset of resident name table
    std::uint32_t  enttab;                  // Offset of Entry Table
    std::uint32_t  dirtab;                  // Offset of Module Directive Table
    std::uint32_t  dircnt;                  // Number of module directives
    std::uint32_t  fpagetab;                // Offset of Fixup Page Table
    std::uint32_t  frectab;                 // Offset of Fixup Record Table
    std::uint32_t  impmod;                  // Offset of Import Module Name Table
    std::uint32_t  impmodcnt;               // Number of entries in Import Module Name Table
    std::uint32_t  impproc;                 // Offset of Import Procedure Name Table
    std::uint32_t  pagesum;                 // Offset of Per-Page Checksum Table
    std::uint32_t  datapage;                // Offset of Enumerated Data Pages
    std::uint32_t  preload;                 // Number of preload pages
    std::uint32_t  nrestab;                 // Offset of Non-resident Names Table
    std::uint32_t  cbnrestab;               // Size of Non-resident Name Table
    std::uint32_t  nressum;                 // Non-resident Name Table Checksum
    std::uint32_t  autodata;                // Object # for automatic data object
    std::uint32_t  debuginfo;               // Offset of the debugging information
    std::uint32_t  debuglen;                // The length of the debugging info. in bytes
    std::uint32_t  instpreload;             // Number of instance pages in preload section of VXD file
    std::uint32_t  instdemand;              // Number of instance pages in demand load section of VXD file
    std::uint32_t  heapsize;                // Size of heap - for 16-bit apps
    std::uint8_t   res3[12];                // Reserved words
    std::uint32_t  winresoff;
    std::uint32_t  winreslen;
    std::uint16_t  devid;                   // Device ID for VxD
    std::uint16_t  ddkver;                  // DDK version for VxD
};

struct VxdObjectHeader {
    std::uint32_t virtualSegmentSize; // In bytes
    std::uint32_t relocationBaseAddress;
    std::uint32_t flags;
    std::uint32_t pageMapIndex;
    std::uint32_t pageMapEntries;
    std::uint32_t unknown;
};

constexpr uint32_t DosExeHeader_lfanew_offset = 0x3C;

constexpr uint32_t ParagraphSize = 16;

void PrintExeHeader(std::FILE* fp, const uint8_t* data, size_t size);

}

#endif
