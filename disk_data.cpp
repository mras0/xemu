#include "disk_data.h"
#include <print>
#include <stdexcept>
#include <cassert>

void DiskData::eject()
{
    data.clear();
    format = DiskFormat {};
    filename.clear();
    file = nullptr;
}

void DiskData::insert(std::vector<uint8_t>&& inData)
{
    if (inData.size() < 512)
        throw std::runtime_error{"Disk is too small"};
    const auto fmt = DiskFormatFromBootSector(inData);
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

    //LOG("Inserting in drive {:02X}: {:?} {} MB", drive, filename, size / (1024.*1024));
    DiskFormat fmt;
    try {
        fmt = DiskFormatFromBootSector(diskData);
    } catch ([[maybe_unused]] const std::exception& e) {
        //LOG("{}", e.what());
        fmt = DiskFormatFromSize(diskData.size());
    }
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