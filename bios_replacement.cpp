#include "bios_replacement.h"
#include "fileio.h"
#include "cpu_flags.h"
#include "disk_data.h"
#include <print>
#include <cstring>
#include <cassert>
#include <fstream>

#define LOG(...) std::println("BIOS: " __VA_ARGS__)
#define UNSUPPORTED(...) throw std::runtime_error{std::format("BIOS: Unspported: " __VA_ARGS__)}
#define CHECK_DISK_PARAMETER(cond)                                 \
    do {                                                           \
        if (!(cond)) {                                             \
            LOG("DISK Invalid paramter: " #cond);                  \
            int13h_setStatus(drive, DiskStatus::InvalidParameter); \
            return;                                                \
        }                                                          \
    } while (0)

#define GET_REG8L(name) static_cast<uint8_t>(cpu_.regs_[REG_##name] & 0xff)
#define GET_REG8H(name) static_cast<uint8_t>((cpu_.regs_[REG_##name] >> 8) & 0xff)
#define GET_REG16(name) static_cast<uint16_t>(cpu_.regs_[REG_##name] & 0xffff)

#define SET_REG8L(name, val) UpdateU8L(cpu_.regs_[REG_##name], val)
#define SET_REG8H(name, val) UpdateU8H(cpu_.regs_[REG_##name], val)
#define SET_REG16(name, val) UpdateU16(cpu_.regs_[REG_##name], val)

namespace {

inline void UpdateU8L(uint64_t& reg, uint8_t value)
{
    reg = (reg & ~0xffULL) | value;
}

inline void UpdateU8H(uint64_t& reg, uint8_t value)
{
    reg = (reg & ~0xff00ULL) | value << 8;
}

[[maybe_unused]] inline void UpdateU16(uint64_t& reg, uint16_t value)
{
    reg = (reg & ~0xffffULL) | value;
}

constexpr uint8_t BiosPort = 0xE9; // Same as BOCHs uses for debug
constexpr uint8_t MaxFloppyDrives = 2;
constexpr uint8_t MaxHardDrives = 1;
constexpr uint8_t MaxDrives = MaxFloppyDrives + MaxHardDrives;

enum class DiskStatus : uint8_t {
    Success = 0x00,
    InvalidParameter = 0x01,
    VerifyFailed = 0x05,
};

}

class BiosReplacement::impl : public MemoryHandler, IOHandler {
public:
    explicit impl(CPU& cpu, SystemBus& bus);

    void insertDisk(uint8_t drive, std::vector<uint8_t>&& data);
    void insertDisk(uint8_t drive, std::string_view filename);

private:
    CPU& cpu_;
    SystemBus& bus_;
    std::vector<uint8_t> romData_;
    struct Drive {
        DiskStatus lastStatus;
        DiskData diskData;

        void clearStatus()
        {
            lastStatus = DiskStatus::Success;
        }
    } drive_[MaxDrives] = {};

    Drive* getDrive(uint8_t drive);
    Drive& getDriveOrDir(uint8_t drive);

    std::uint8_t readU8([[maybe_unused]] std::uint64_t addr, [[maybe_unused]] std::uint64_t offset) override
    {
        return romData_[offset & (romData_.size() - 1)];
    }

    void writeU8(std::uint64_t addr, [[maybe_unused]] std::uint64_t offset, std::uint8_t value) override
    {
        std::println("BIOS: Write to {:X} value {:02X}", addr, value);
    }

    virtual void outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value) override;

    void int13h_setStatus(Drive* drive, DiskStatus status);
    void int13h_00_reset();
    void int13h_diskOp(uint8_t operation);
    void int13h_08_getDriveParameters();
    void int13h_15_getDiskType();
};

BiosReplacement::impl::impl(CPU& cpu, SystemBus& bus)
    : cpu_ { cpu }
    , bus_ { bus }
    , romData_ { ReadFile("bios/bios.bin") }
{
    // Mirror ROM to fill out last 64KB (FreeDOS scans this range to check for vvmware/qemu)
    bus_.addMemHandler(0x100000 - 64 * 1024, 64 * 1024, *this);
    bus_.addIOHandler(BiosPort, 1, *this);
}

BiosReplacement::impl::Drive* BiosReplacement::impl::getDrive(uint8_t drive)
{
    if (drive & 0x80) {
        drive &= 0x7f;
        if (drive >= MaxHardDrives)
            return nullptr;
        drive += MaxFloppyDrives;
    } else {
        if (drive >= MaxFloppyDrives)
            return nullptr;
    }
    return &drive_[drive];
}

BiosReplacement::impl::Drive& BiosReplacement::impl::getDriveOrDir(uint8_t drive)
{
    auto dr = getDrive(drive);
    if (!dr)
        UNSUPPORTED("drive {}", drive);
    return *dr;
}

void BiosReplacement::impl::insertDisk(uint8_t drive, std::vector<uint8_t>&& data)
{
    LOG("Inserting in drive {:02X} size {} KB format", drive, data.size() / 1024.);
    auto& dr = getDriveOrDir(drive);
    dr.diskData.insert(std::move(data));
    dr.clearStatus();
}

void BiosReplacement::impl::insertDisk(uint8_t drive, std::string_view filename)
{
    auto& dr = getDriveOrDir(drive);
    if (filename.empty())
        LOG("Ejecting disk from drive {:02X}", drive);
    else
        LOG("Inserting disk in drive {:02X}: {:?}", drive, filename);
    dr.diskData.insert(filename);
    dr.clearStatus();
    const auto& fmt = dr.diskData.format;
    LOG("Format: {}/{}/{}", fmt.numCylinder, fmt.headsPerCylinder, fmt.sectorsPerTrack);
}

void BiosReplacement::impl::outU16([[maybe_unused]] std::uint16_t port, [[maybe_unused]] std::uint16_t offset, [[maybe_unused]] std::uint16_t value)
{
    switch (value) {
    case 0x1300:
        int13h_00_reset();
        break;
    case 0x1302: // READ
    case 0x1303: // WRITE
    case 0x1304: // VERIFY
        int13h_diskOp(value & 0xff);
        break;
    case 0x1308:
        int13h_08_getDriveParameters();
        break;
    case 0x1315:
        int13h_15_getDiskType();
        break;
    case 0x1318: { // SET MEDIA TYPE FOR FORMAT

        // TODO: Initialize in Extended BIOS Data Area
        // 3Dh 16 BYTEs	Fixed Disk parameter table for drive 0
        // And return in ES:DI
        //https://stanislavs.org/helppc/int_13-18.html
        //https://stanislavs.org/helppc/dbt.html

        const auto driveNum = GET_REG8L(DX);
        LOG("INT13h/18 SET MEDIA TYPE FOR FORMAT drive = {:02X} CX={:04X}", driveNum, GET_REG16(CX));
#if 1
        int13h_setStatus(nullptr, DiskStatus::InvalidParameter); // Not supported
#else
        auto drive = getDrive(driveNum);
        CHECK_DISK_PARAMETER(drive != nullptr);
        CHECK_DISK_PARAMETER(driveNum & 0x80);
        cpu_.sregs_[SREG_ES] = 0x40;
        cpu_.sdesc_[SREG_ES].base = cpu_.sregs_[SREG_ES] * 16;
        const uint32_t physBase = (uint32_t)(cpu_.sdesc_[SREG_ES].base + 0xD0);
        SET_REG16(DI, static_cast<uint16_t>(physBase & 0xff));
        uint8_t mediaFormat[11] = "HelloWld!";
        for (size_t i = 0; i < sizeof(mediaFormat); ++i)
            bus_.writeU8(physBase + i, mediaFormat[i]);
        int13h_setStatus(drive, DiskStatus::Success);
#endif
        break;
    }
    case 0x1341: // Extensions installation check (not supported)
        break;
    case 0x1900:
        LOG("Boot hook");
        if (0) {
            ///const char* filename = "../misc/asmtest/keyboard/test.com";
            const char* filename = "../misc/asmtest/mode6/test.com";
            LOG("Loading test: {}", filename);
            auto data = ReadFile(filename);
            for (size_t i = 0; i < data.size(); ++i)
                bus_.writeU8(0x10100 + i, data[i]);
            cpu_.flags_ &= ~EFLAGS_MASK_ZF;
        } else {
            cpu_.flags_ |= EFLAGS_MASK_ZF;
        }
        break;
    case 0xFEDE: {
        //cpu_.showHistory(1);
        const auto AH = GET_REG8H(BP);
        const auto AL = GET_REG8L(BP);
        const auto BL = GET_REG8L(BX);
        const auto CX = GET_REG16(CX);
        const auto DX = GET_REG16(DX);
        switch (AH) {
        case 0x01: // Set cursor shape
            break;
        case 0x02: // Set cursor position
        {
            static int cnt = 0;
            if (cnt < 5000) {
                LOG("Set Cursor {},{}", DX & 0xff, DX >> 8);
                ++cnt;
            }
            break;
        }
        case 0x03: // Get cursor position
            break;
        case 0x06:
        case 0x07:
            LOG("Scroll {} ({}, {}) ({}, {}) NumLines={} Attr={:02X}", AH == 6 ? "up" : "down", CX & 0xff, CX >> 8, DX & 0xff, DX >> 8, AL, BL);
            break;
        case 0x08: // Read char
            break;
        case 0x09:
        case 0x0E:
            LOG("AH={:02X} Write '{:c}' ({:02X}) Color={:02X} CX={}", AH, AL >= ' ' && AL < 0x80 ? AL : '?', AL, BL, CX);
            break;
        default:
            LOG("AH={:02X}", AH);
            break;
        }
        break;
    }
    default:
        throw std::runtime_error { std::format("{} BIOS TODO: Hack port with {:04X}", cpu_.currentIp(), value) };
    }
}

void BiosReplacement::impl::int13h_setStatus(BiosReplacement::impl::Drive* drive, DiskStatus status)
{
    if (drive)
        drive->lastStatus = status;
    if (status == DiskStatus::Success)
        cpu_.flags_ &= ~EFLAGS_MASK_CF;
    else
        cpu_.flags_ |= EFLAGS_MASK_CF;
    // AH = status
    SET_REG8H(AX, static_cast<uint8_t>(status));
}

void BiosReplacement::impl::int13h_00_reset()
{
    const auto driveNum = GET_REG8L(DX);
    LOG("INT13h/00 Reset drive = {:02X}", driveNum);
    auto drive = getDrive(driveNum);
    CHECK_DISK_PARAMETER(drive != nullptr);
    int13h_setStatus(drive, DiskStatus::Success);
}

void BiosReplacement::impl::int13h_diskOp(uint8_t op)
{
    const auto driveNum = GET_REG8L(DX);
    const auto numSectors = GET_REG8L(BP);
    const auto cylinder = GET_REG8H(CX) | (GET_REG8L(CX) & 0xC0) << 2;
    const auto sectorNumber = GET_REG8L(CX) & 0x3F;
    const auto head = GET_REG8H(DX);
    const auto seg = cpu_.sregs_[SREG_ES];
    const auto ofs = GET_REG16(BX);

    assert(op == 2 || op == 3 ||  op == 4);

    LOG("INT13h/{:02X} {} drive = {:02X}, C/H/S {}/{}/{} count={} Dest={:04X}:{:04X}", op, op == 2 ? "Read" : op == 3 ? "Write" : "Verify", driveNum, cylinder, head, sectorNumber, numSectors, seg, ofs);

    auto drive = getDrive(driveNum);
    CHECK_DISK_PARAMETER(drive != nullptr);
    CHECK_DISK_PARAMETER(drive->diskData.format.validCHS(cylinder, head, sectorNumber));
    const auto srcAddr = drive->diskData.format.toLBA(cylinder, head, sectorNumber) * bytesPerSector;
    const auto byteCount = bytesPerSector * numSectors;
    CHECK_DISK_PARAMETER(srcAddr + byteCount <= drive->diskData.data.size());

    // Note: Verify doesn't actually compare data, it just checks that it was written correctly.
    if (op == 4) {
        int13h_setStatus(drive, DiskStatus::Success);
        return;
    }

    for (uint32_t i = 0; i < byteCount; ++i) {
        const auto addr = (seg * 16) + ((ofs + i) & 0xffff);
        ///const auto addr = (seg * 16) + (ofs + i);
        if (op == 2) {
            bus_.writeU8(addr, drive->diskData.data[srcAddr + i]);
        } else if (op == 3) {
            drive->diskData.data[srcAddr + i] = bus_.readU8(addr);
        }
    }

    if (op == 3)
        drive->diskData.afterWrite(srcAddr, byteCount);

    int13h_setStatus(drive, DiskStatus::Success);
}

void BiosReplacement::impl::int13h_08_getDriveParameters()
{
    const auto driveNum = GET_REG8L(DX);
    LOG("INT13h/08 Get Drive Paramters drive = {:02X}", driveNum);
    auto drive = getDrive(driveNum);
    CHECK_DISK_PARAMETER(drive != nullptr);
    CHECK_DISK_PARAMETER(drive->diskData.format.numCylinder != 0);
    const auto cylMax = drive->diskData.format.numCylinder - 1;
    SET_REG8L(BX, 0); // BL = drive type (ignore)
    SET_REG8H(CX, static_cast<uint8_t>(cylMax)); // CH = low eight bits of maximum cylinder number
    SET_REG8L(CX, static_cast<uint8_t>(((cylMax >> 2) & 0xC0) | drive->diskData.format.sectorsPerTrack));
    SET_REG8H(DX, static_cast<uint8_t>(drive->diskData.format.headsPerCylinder - 1));
    SET_REG8L(DX, bus_.readU8(0x475)); // DL = number of harddrives
    int13h_setStatus(drive, DiskStatus::Success);
}

void BiosReplacement::impl::int13h_15_getDiskType()
{
    // XXX: MS-DOS 5.0 crashes with this implementation
    const auto driveNum = GET_REG8L(DX);
    LOG("INT13h/15 Get Disk Type drive = {:02X}", driveNum);
#if 0
    auto drive = getDrive(driveNum);
    CHECK_DISK_PARAMETER(drive != nullptr);
    CHECK_DISK_PARAMETER(drive->format.numCylinder != 0);
    // AL = drive type
    if (driveNum & 0x80)
        SET_REG8L(AX, 2);
    else
        SET_REG8L(AX, 3);
    // CX:DX = number of sectors
    const auto numSectors = drive->format.totalSectors();
    SET_REG16(CX, static_cast<uint16_t>(numSectors >> 16));
    SET_REG16(DX, static_cast<uint16_t>(numSectors));
    int13h_setStatus(drive, DiskStatus::Success);
#else
    int13h_setStatus(nullptr, DiskStatus::InvalidParameter);
#endif
}

BiosReplacement::BiosReplacement(CPU& cpu, SystemBus& bus)
    : impl_ { std::make_unique<impl>(cpu, bus) }
{
}

BiosReplacement::~BiosReplacement() = default;

void BiosReplacement::insertDisk(uint8_t drive, std::vector<uint8_t>&& data)
{
    impl_->insertDisk(drive, std::move(data));
}

void BiosReplacement::insertDisk(uint8_t drive, std::string_view filename)
{
    impl_->insertDisk(drive, filename);
}