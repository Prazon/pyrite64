/**
* Line2D component.
* Single-color screen-space line from obj.pos to (obj.pos + delta). The
* delta is stored as a signed pixel pair, matching the model every other
* 2D component uses: obj.pos is the anchor, the component carries an
* offset relative to it.
*
* Implementation uses rdpq_fill_rectangle for axis-aligned cases (cheap,
* one rect) and a sequence of small fills for diagonal cases (Bresenham,
* one fill per row of the major axis). The runtime stays in fill mode so
* there's no texture binding cost.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Line2D
  {
    static constexpr uint32_t ID = 25;

    int16_t  dx{16};            // end-point offset relative to obj.pos
    int16_t  dy{0};
    uint8_t  r{255};
    uint8_t  g{255};
    uint8_t  b{255};
    uint8_t  a{255};
    uint8_t  thickness{1};
    uint8_t  _pad0{0};
    uint8_t  _pad1{0};
    uint8_t  _pad2{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Line2D);
    }

    static void initDelete(Object& obj, Line2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Line2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Line2D* data, float deltaTime);
  };
}
