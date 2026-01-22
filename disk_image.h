#ifndef DISK_IMAGE_H
#define DISK_IMAGE_H

#include <memory>
#include <string>
#include <string_view>
#include "disk_data.h"

static constexpr uint8_t DE_ATTR_MASK_READ_ONLY = 1 << 0;
static constexpr uint8_t DE_ATTR_MASK_HIDDEN = 1 << 1;
static constexpr uint8_t DE_ATTR_MASK_SYSTEM = 1 << 2;
static constexpr uint8_t DE_ATTR_MASK_VOLUME_LABEL = 1 << 3;
static constexpr uint8_t DE_ATTR_MASK_DIRECTORY = 1 << 4;
static constexpr uint8_t DE_ATTR_MASK_ARCHIVE = 1 << 5;
static constexpr uint8_t DE_ATTR_MASK_DEVICE = 1 << 6;

struct DirectoryEntry {
    std::string filename;
    uint32_t size;
    uint32_t cluster;
    uint8_t attribute;
    int64_t modTime;
};

class DiskImage {
public:
    explicit DiskImage(DiskData& diskData);
    ~DiskImage();

    uint32_t numPartitions() const;
    void setPartition(uint32_t index);
    std::vector<DirectoryEntry> dirEntries(std::string_view path);
    std::vector<uint8_t> readFile(std::string_view path);
    void writeFile(std::string_view path, const void* data, uint32_t size);
    void makeDir(std::string_view path);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

void FormatDosDisk(DiskData& data);

#endif
