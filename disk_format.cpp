#include "disk_format.h"
#include <format>
#include <stdexcept>

static const DiskFormat* diskFormats[] = {
    // Floppy formats
    &diskFormat180K,
    &diskFormat360K,
    &diskFormat720K,
    &diskFormat1440K,
    &diskFormat1680K,
    // Hard drive formats
    &diskFormatST157A,
    &diskFormatST1133A,
    &diskFormatSL520,
};

const DiskFormat& DiskFormatFromMediaDescriptor(uint8_t mediaDescriptor)
{
    switch (mediaDescriptor) {
    case 0xF0:
        return diskFormat1440K;
    case 0xF9:
        return diskFormat720K;
    case 0xFC:
        return diskFormat180K;
    case 0xFD:
        return diskFormat360K;
    default:
        throw std::runtime_error { std::format("Unsupported media descriptor {:02X}", mediaDescriptor ) };
    }
}

const DiskFormat& DiskFormatFromBootSector(const uint8_t* data, size_t size)
{
    // TODO: Read drive geometry for harddisks if available
    assert(size % bytesPerSector == 0);
    assert(size >= bytesPerSector);
    const auto mediaDescriptor = data[0x15];
    const auto& format = DiskFormatFromMediaDescriptor(mediaDescriptor);
    if (format.sizeInBytes() != size)
        throw std::runtime_error { std::format("Unexpected disk size {} KB for media descriptor 0x{:02X}", size/1024., mediaDescriptor) };
    return format;
}

const DiskFormat& DiskFormatFromBootSector(const std::vector<uint8_t>& data)
{
    assert(data.size() >= bytesPerSector);
    return DiskFormatFromBootSector(data.data(), data.size());
}

const DiskFormat& DiskFormatFromSize(size_t size)
{
    for (const auto fmt : diskFormats) {
        if (fmt->sizeInBytes() == size)
            return *fmt;
    }
    throw std::runtime_error {std::format("Unable to determine disk format from size {} MB", size / (1024. * 1024.)) };
}