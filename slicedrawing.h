#pragma once

#include <Arduino.h>
#include "types.h"

namespace SliceDrawing {
    void clear();

    void fillcolor(uint8_t y1, uint8_t y2, uint16_t color);
    void fulltline(uint8_t y, uint8_t mapid, Vec2D start, Vec2D step);
    void scaledsprite(uint8_t spriteid, int16_t x, int16_t y, uint8_t w, uint8_t h);
    void hline(uint8_t x, uint8_t y, uint8_t w, uint16_t color);

    void filledsquare(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

    void flush(uint16_t* buffer, uint8_t slice);

    uint16_t getBufferSize();
}