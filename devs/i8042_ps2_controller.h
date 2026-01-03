#ifndef I8042_PS2_CONTROLLER
#define I8042_PS2_CONTROLLER

#include "system_bus.h"
#include "keyboard.h"
#include <functional>
#include <memory>

class i8042_PS2Controller {
public:
    using CallbackType = std::function<void(void)>;
    using A20CallbackType =std::function<void(bool)>;
    explicit i8042_PS2Controller(SystemBus& bus, CallbackType onDevice1IRQ, CallbackType onDevice2IRQ, A20CallbackType onA20CLinehange);
    ~i8042_PS2Controller();

    void enqueueKey(const KeyPress& key);
    void mouseMove(int dx, int dy);
    void mouseButton(int idx, bool down);
    void mouseUpdate();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
