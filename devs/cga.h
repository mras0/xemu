#ifndef CGA_H
#define CGA_H

#include <memory>
#include <functional>
#include "system_bus.h"

class CGA {
public:
    using DrawFunction = std::function<void (const uint32_t* pixels, int w, int h)>;

    explicit CGA(SystemBus& bus);
    ~CGA();

    void setDrawFunction(const DrawFunction& onDraw);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
