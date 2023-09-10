#pragma once

typedef uint8_t* bytebuffer;

struct bitTightBuffer {
    bytebuffer buffer;
    uint16_t buffer_max;

    uint16_t size;

    uint32_t cursor;
    uint8_t last_byte;

    inline void clear() {
        size = 0;
        cursor = 0;
    }

    template<typename T>
    T readBits(uint8_t amount) {
        T ret = 0;

        uint8_t bitoffset = cursor & 0b111;
        if (bitoffset==0) {
            last_byte = buffer[cursor>>3];
        }

        amount = last_byte >> bitoffset;
        // algorithm not complete as I reconsidered its usefulness while making it
    }
};