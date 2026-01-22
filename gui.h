#ifndef GUI_H
#define GUI_H

#include <cstdint>
#include <memory>
#include <vector>
#include "event.h"

class GUI {
public:
    explicit GUI(int w, int h, int guiScale);
    ~GUI();

    std::vector<Event> update();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

void SetGuiActive(bool active);
void DrawScreen(const uint32_t* pixels, int w, int h);

#endif