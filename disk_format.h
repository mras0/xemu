#ifndef DISK_FORMAT_H
#define DISK_FORMAT_H

#include <stdint.h>
#include <cassert>
#include <vector>

constexpr uint32_t bytesPerSector = 512;
struct DiskFormat {
    uint32_t numCylinder;
    uint32_t headsPerCylinder; // AKA sides
    uint32_t sectorsPerTrack;

    constexpr uint32_t totalSectors() const
    {
        return numCylinder * headsPerCylinder * sectorsPerTrack;
    }

    constexpr uint64_t sizeInBytes() const
    {
        return totalSectors() * bytesPerSector;
    }

    constexpr bool validCHS(uint32_t cylinder, uint32_t head, uint32_t sector) const
    {
        return cylinder < numCylinder && head < headsPerCylinder && sector > 0 && sector <= sectorsPerTrack;
    }

    constexpr uint32_t toLBA(uint32_t cylinder, uint32_t head, uint32_t sector) const
    {
        assert(validCHS(cylinder, head, sector));
        return (cylinder * headsPerCylinder + head) * sectorsPerTrack + (sector - 1);
    }
};

static constexpr DiskFormat diskFormat180K = { 40, 1, 9 }; // 180KB 5.25", 0xFC media descriptor
static constexpr DiskFormat diskFormat360K = { 40, 2, 9 }; // 360KB 5.25", 0xFD media descriptor
static constexpr DiskFormat diskFormat720K = { 80, 2, 9 }; // 720KB 3.5", 0xF9 media descriptor
static constexpr DiskFormat diskFormat1440K = { 80, 2, 18 }; // 1440KB 3.5", 0xF0 media descriptor

static constexpr DiskFormat diskFormatST157A {
    560, 6, 26 // 43680 KB - SEAGATE: ST157A-1 45MB 3.5"/HH IDE / AT
};

const DiskFormat& DiskFormatFromMediaDescriptor(uint8_t mediaDescriptor);

const DiskFormat& DiskFormatFromBootSector(const uint8_t* data, size_t size);
const DiskFormat& DiskFormatFromBootSector(const std::vector<uint8_t>& data);
const DiskFormat& DiskFormatFromSize(size_t size);

#endif
