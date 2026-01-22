#ifndef EVENT_H
#define EVENT_H

#include <cstdint>
#include "keyboard.h"

enum class EventType {
    quit,
    keyboard,
    diskInsert,
    diskEject,
    diskExport,
    mouseMove,
    mouseButton,
};

struct Event {
    EventType type;
    union {
        KeyPress key;
        struct {
            std::uint8_t drive;
            char filename[256]; // FIXME
        } diskInsert, diskExport;
        struct {
            std::uint8_t drive;
        } diskEject;
        struct {
            int dx, dy;
        } mouseMove;
        struct {
            int index;
            bool down;
        } mouseButton;
    };
};

#endif
