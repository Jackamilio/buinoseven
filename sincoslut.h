#pragma once

#include "sin1.h"

inline FP sin(int16_t turns) {
    return FP::fromInternal(sin1(turns)<<1);
}

inline FP cos(int16_t turns) {
    return FP::fromInternal(cos1(turns)<<1);
}


// inline FP sin(FP turns) {
//     return FP::fromInternal(sin1(turns.getFraction()>>1));
// }

// inline FP cos(FP turns) {
//     return FP::fromInternal(cos1(turns.getFraction()>>1));
// }