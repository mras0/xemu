#ifndef DMA_HANDLER
#define DMA_HANDLER

class DMAHandler {
public:
    virtual uint8_t dmaGetU8() = 0;
    virtual void dmaPutU8(uint8_t data) = 0;
    virtual void dmaDone() = 0;
};

constexpr uint8_t DMA_CHANNEL_FLOPPY = 2;

#endif
