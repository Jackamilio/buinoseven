#pragma once

#include "types.h"

template<typename T>
inline bool valueInside(const T& value, const T& left, const T& right) {
  return value >= left and value <= right;
}

template<typename T>
inline T lerp(const T& from, const T& to, const T& t) {
  return from + (to-from) * t;
}

template<typename T>
inline T sign(const T& val) {
  return (val < 0) ? -1 : 1;
}

template<typename T>
inline T crawl(const T& val, const T& target, const T& step) {
  T d=target-val;
  if (abs(d) < step) {
    return target;
  }
  else {
    return val + sign(d) * step;
  }
}

inline bool pointInRect(const Vec2D& p1, const Vec2D& p2, const Vec2D& size) {
  return valueInside(p1(0),p2(0),p2(0)+size(0)) && valueInside(p1(1),p2(1),p2(1)+size(1));
}

inline bool pointInScreen(const Vec2D& p) {
  return valueInside(p(0),FP(0),FP(160)) && valueInside(p(1),FP(0),FP(128));
}
