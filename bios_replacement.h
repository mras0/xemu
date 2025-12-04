#ifndef BIOS_REPLACEMENT_H
#define BIOS_REPLACEMENT_H

#include <memory>
#include <vector>
#include <string_view>
#include "cpu.h"
#include "system_bus.h"

class BiosReplacement {
public:
    explicit BiosReplacement(CPU& cpu, SystemBus& bus);
    ~BiosReplacement();

    void insertDisk(uint8_t drive, std::vector<uint8_t>&& data);
    void insertDisk(uint8_t drive, std::string_view filename);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
