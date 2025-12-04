#ifndef DISK_DATA
#define DISK_DATA

#include <vector>
#include <string_view>
#include <string>
#include <memory>
#include <fstream>
#include "disk_format.h"

struct DiskData {
    std::vector<uint8_t> data;
    DiskFormat format;
    std::string filename;
    std::unique_ptr<std::fstream> file;

    void eject();
    void insert(std::vector<uint8_t>&& data);
    void insert(std::string_view filename);
    void afterWrite(size_t offset, size_t count);
};

void CreateDisk(std::string_view filename, const DiskFormat& fmt);

#endif
