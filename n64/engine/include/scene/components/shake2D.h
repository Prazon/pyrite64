/**
* Shake2D component.
* Adds a random per-frame pixel offset to obj.pos for `framesLeft`
* updates, then restores the captured baseline. Designed for cell-level
* effects where the underlying anchor position is otherwise static —
* exactly the model Pixic's bomb-cell shake uses.
*
* On Shake2D::trigger(obj, data, magnitudePx, frames), the current obj.
* pos is captured as the baseline. While active, each update sets obj.
* pos = baseline + (random +/- mag). When framesLeft hits zero obj.pos
* is reset to the baseline.
*
* This intentionally modifies obj.pos directly rather than via a draw-
* time offset because other 2D components read obj.pos and would
* otherwise need to know about a side channel. The trade-off is that
* anything else moving the object during a shake will be overwritten.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Shake2D
  {
    static constexpr uint32_t ID = 29;

    float    baselineX{0};
    float    baselineY{0};
    uint16_t framesLeft{0};
    uint8_t  magnitude{1};   // pixel amplitude
    uint8_t  active{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Shake2D);
    }

    static void initDelete(Object& obj, Shake2D* data, void* initData);
    static void update(Object& obj, Shake2D* data, float deltaTime);
    static void draw([[maybe_unused]] Object& obj, [[maybe_unused]] Shake2D* data,
                     [[maybe_unused]] float deltaTime) {}

    static void trigger(Object &obj, Shake2D* data, uint8_t magnitudePx, uint16_t frames);
    static void stop(Object &obj, Shake2D* data);
  };
}
