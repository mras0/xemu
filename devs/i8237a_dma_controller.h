#ifndef I8237A_DMA_CONTROLLER
#define I8237A_DMA_CONTROLLER

#include "system_bus.h"
#include "dma_handler.h"
#include <memory>

class i8237a_DMAController {
public:
    explicit i8237a_DMAController(SystemBus& bus, uint16_t ioBase, uint16_t pageIoBase, bool wordMode);
    ~i8237a_DMAController();

    void startGet(uint8_t channel, DMAHandler& handler);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
