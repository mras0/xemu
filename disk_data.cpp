#include "disk_data.h"
#include <print>
#include <stdexcept>
#include <cassert>
#include <cstring>

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

DiskData::DiskData()
{
    eject();
}

void DiskData::eject()
{
    data_.clear();
    format_ = DiskFormat {};
    filename_.clear();
    file_ = nullptr;
}

void DiskData::insert(std::vector<uint8_t>&& inData)
{
    const auto fmt = DiskFormatFromData(inData);
    eject();
    data_ = std::move(inData);
    format_ = fmt;
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
    data_ = std::move(diskData);
    format_ = fmt;
    file_ = std::move(diskFile);
    filename_ = diskFilename;
}

void DiskData::afterWrite(size_t offset, size_t count)
{
    assert(offset < data_.size() && offset + count <= data_.size());
    if (!file_)
        return;
    file_->seekp(offset, std::ios::beg);
    if (!*file_)
        throw std::runtime_error { std::format("File seek failed. Address = {:X} for {:?}.", offset, filename_) };
    file_->write(reinterpret_cast<const char*>(&data_[offset]), count);
    if (!*file_)
        throw std::runtime_error { std::format("HD file write failed. Address = {:X} Count = {:X} for {:?}", offset, count, filename_) };
}

void DiskData::read(void* buffer, size_t offset, size_t count)
{
    if (offset > sizeInBytes() || offset + count > sizeInBytes())
        throw std::runtime_error { std::format("Disk read out of bounds for offset {:X} count {:X} (size {:X})", offset, count, sizeInBytes()) };
    std::memcpy(buffer, &data_[offset], count);
}

void DiskData::write(const void* buffer, size_t offset, size_t count)
{
    if (offset > sizeInBytes() || offset + count > sizeInBytes())
        throw std::runtime_error { std::format("Disk write out of bounds for offset {:X} count {:X} (size {:X})", offset, count, sizeInBytes()) };
    std::memcpy(&data_[offset], buffer, count);
    afterWrite(offset, count);
}

uint8_t DiskData::readU8(size_t offset)
{
    uint8_t value;
    read(&value, offset, 1);
    return value;
}

void DiskData::writeU8(uint8_t value, size_t offset)
{
    write(&value, offset, 1);
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