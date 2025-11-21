#ifndef GUI_H
#define GUI_H

#include <cstdint>
#include <memory>
#include <vector>

class GUI {
public:
    explicit GUI(int w, int h, int xscale = 1, int yscale = 1);
    ~GUI();

    enum class EventType {
        quit,
        keyboard
    };

    struct Event {
        EventType type;
        union {
            struct {
                bool down;
                uint32_t scanCode;
            } key;
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