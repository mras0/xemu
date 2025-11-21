#ifndef NEC765_FLOPPY_CONTROLLER
#define NEC765_FLOPPY_CONTROLLER

#include "system_bus.h"
#include "dma_handler.h"
#include <functional>
#include <memory>


class NEC765_FloppyController {
public:
    using OnInterrupt = std::function<void(void)>;
    using OnDmaStart = std::function<void (bool isPut, DMAHandler& handler)>;

    explicit NEC765_FloppyController(SystemBus& bus, const OnInterrupt& onInt, const OnDmaStart& onDmaStart);
    ~NEC765_FloppyController();

    void insertDisk(uint8_t drive, const std::vector<uint8_t>& data);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
