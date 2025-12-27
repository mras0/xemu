#ifndef ATA_CONTROLLER_H
#define ATA_CONTROLLER_H

#include "system_bus.h"
#include <functional>
#include <memory>
#include <string_view>

class ATAController {
public:
    using onIrqType = std::function<void(void)>;

    explicit ATAController(SystemBus& bus, uint16_t baseRegister, uint16_t controlRegister, onIrqType onIrq);
    ~ATAController();

    void insertDisk(uint8_t driveNum, std::string_view filename);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
