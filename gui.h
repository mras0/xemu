#ifndef GUI_H
#define GUI_H

#include <cstdint>
#include <memory>
#include <vector>
#include "keyboard.h"

class GUI {
public:
    explicit GUI(int w, int h, int xscale = 1, int yscale = 1);
    ~GUI();

    enum class EventType {
        quit,
        keyboard,
        diskInsert,
        diskEject,
    };

    struct Event {
        EventType type;
        union {
            KeyPress key;
            struct {
                std::uint8_t drive;
                char filename[256]; // FIXME
            } diskInsert;
            struct {
                std::uint8_t drive;
            } diskEject;
        };
    };

    std::vector<Event> update();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

void SetGuiActive(bool active);
void DrawScreen(const uint32_t* pixels);

#endif