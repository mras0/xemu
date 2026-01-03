#ifndef VGA_H
#define VGA_H

#include "system_bus.h"
#include <memory>
#include <functional>

class VGA {
public:
    explicit VGA(SystemBus& bus, bool egaOnly);
    ~VGA();

    using DrawFunction = std::function<void(const uint32_t* pixels, int w, int h)>;
    void setDrawFunction(const DrawFunction& onDraw);
    void registerDebugFunction(class Debugger& dbg);

    void forceRedraw();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};


#endif
