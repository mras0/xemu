#ifndef DISK_DATA
#define DISK_DATA

#include <vector>
#include <string_view>
#include <string>
#include <memory>
#include <fstream>
#include "disk_format.h"

class DiskData {
public:
    DiskData();

    const DiskFormat& format() const
    {
        return format_;
    }

    const std::string& filename() const
    {
        return filename_;
    }

    size_t sizeInBytes() const
    {
        return data_.size();
    }

    void eject();
    void insert(std::vector<uint8_t>&& data);
    void insert(std::string_view filename);
    void afterWrite(size_t offset, size_t count);

    void read(void* buffer, size_t offset, size_t count);
    void write(const void* buffer, size_t offset, size_t count);

    uint8_t readU8(size_t offset);
    void writeU8(uint8_t value, size_t offset);

private:
    std::vector<uint8_t> data_;
    DiskFormat format_;
    std::string filename_;
    std::unique_ptr<std::fstream> file_;
};

void CreateDisk(std::string_view filename, const DiskFormat& fmt);

#endif
