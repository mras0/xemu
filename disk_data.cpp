#include "disk_data.h"
#include <print>
#include <stdexcept>
#include <cassert>

namespace {

DiskFormat DiskFormatFromData(const std::vector<uint8_t>& data)
{
    //static constexpr DiskFormat diskFormat180K = { 40, 1, 9 };
    if (data.size() < bytesPerSector)
        throw std::runtime_error { "Disk is too small" };
    try {
        return DiskFormatFromBootSector(data);
    } catch ([[maybe_unused]] const std::exception& e) {
        try {
            return DiskFormatFromSize(data.size());
        } catch ([[maybe_unused]] const std::exception& e2) {
            // Fake up a single sided format for small disks
            const auto cylSize = 9 * bytesPerSector;
            const auto numCyls = data.size() / cylSize;
            if (data.size() % cylSize || !numCyls || numCyls > 40)
                throw std::runtime_error { "Disk size is wrong for fake format" };
            return DiskFormat { static_cast<uint32_t>(numCyls), 1U, 9U };
        }
    }
}

} // unnamed namespace

void DiskData::eject()
{
    data.clear();
    format = DiskFormat {};
    filename.clear();
    file = nullptr;
}

void DiskData::insert(std::vector<uint8_t>&& inData)
{
    const auto fmt = DiskFormatFromData(inData);
    eject();
    data = std::move(inData);
    format = fmt;
}

void DiskData::insert(std::string_view diskFilename)
{
    if (diskFilename.empty()) {
        eject();
        return;
    }

    auto diskFile = std::make_unique<std::fstream>(std::string(diskFilename), std::ios::in | std::ios::out | std::ios::binary);
    if (!*diskFile)
        throw std::runtime_error { std::format("Could not open {:?} for insertion", diskFilename) };

    diskFile->seekg(0, std::ios::end);
    const uint64_t size = diskFile->tellg();
    std::vector<uint8_t> diskData(size);
    diskFile->seekg(0);

    if (size < bytesPerSector || !diskFile)
        throw std::runtime_error { std::format("Failed to determine size of {:?}", diskFilename) };

    diskFile->read(reinterpret_cast<char*>(&diskData[0]), diskData.size());
    if (!*diskFile)
        throw std::runtime_error { std::format("Failed to read from {:?}", diskFilename) };

    DiskFormat fmt = DiskFormatFromData(diskData);
    //LOG("Format: {}/{}/{}", fmt.numCylinder, fmt.headsPerCylinder, fmt.sectorsPerTrack);
    eject();
    data = std::move(diskData);
    format = fmt;
    file = std::move(diskFile);
    filename = diskFilename;
}

void DiskData::afterWrite(size_t offset, size_t count)
{
    assert(offset < data.size() && offset + count <= data.size());
    if (!file)
        return;
    file->seekp(offset, std::ios::beg);
    if (!*file)
        throw std::runtime_error { std::format("File seek failed. Address = {:X} for {:?}.", offset, filename) };
    file->write(reinterpret_cast<const char*>(&data[offset]), count);
    if (!*file)
        throw std::runtime_error { std::format("HD file write failed. Address = {:X} Count = {:X} for {:?}", offset, count, filename) };
}

void CreateDisk(std::string_view filename, const DiskFormat& fmt)
{
    {
        std::ofstream of { std::string(filename), std::ios::in | std::ios::binary };
        if (of)
            throw std::runtime_error { std::format("{:?} already exists", filename) };
    }


    std::vector<char> data(bytesPerSector * 16);
    size_t numBytes = fmt.sizeInBytes();
    if (numBytes % data.size())
        throw std::runtime_error { "Invalid disk format" };

    std::ofstream of { std::string(filename), std::ios::out | std::ios::binary };
    if (!of)
        throw std::runtime_error { std::format("Could not create {:?}", filename) };
    for (size_t i = 0; i < numBytes; i += data.size())
        of.write(data.data(), data.size());
}