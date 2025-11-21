#include "gui.h"

class GUI::impl {
public:
};

GUI::GUI([[maybe_unused]] int w, [[maybe_unused]] int h, [[maybe_unused]] int xscale, [[maybe_unused]] int yscale)
{}

GUI::~GUI() = default;

std::vector<GUI::Event> GUI::update()
{
    return {};
}

void SetGuiActive([[maybe_unused]] bool active)
{
}

void DrawScreen([[maybe_unused]] const uint32_t* pixels)
{
}
