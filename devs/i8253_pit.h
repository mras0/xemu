#ifndef I8253_PIT
#define I8253_PIT

#include "system_bus.h"
#include <functional>

class i8253_PIT : public CycleObserver, public IOHandler {
public:
    using CallbackType = std::function<void(void)>;

    explicit i8253_PIT(SystemBus& bus, CallbackType cb);

    void reset();

    void runCycles(std::uint64_t numCycles) override;
    std::uint64_t nextAction() override;

    std::uint8_t inU8(uint16_t port, uint16_t) override;
    void outU8(uint16_t port, uint16_t, std::uint8_t value) override;

private:
    SystemBus& bus_;
    CallbackType cb_;
    std::uint64_t cycles_;
    struct Channel {
        uint8_t control;
        uint16_t initialCount;
        uint16_t counter;
        uint16_t latch;
        bool msb;
        bool reload;
        bool active;

        bool clock(SystemBus& bus);
    } channel_[3];
};

#endif
