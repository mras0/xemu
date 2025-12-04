#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <cstdint>

struct KeyPress {
    bool down;
    bool extendedKey;
    std::uint8_t scanCode; // Always scan code set 1
};

#endif
