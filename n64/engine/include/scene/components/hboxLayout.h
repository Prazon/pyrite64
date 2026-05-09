/**
* HBoxLayout component.
* Re-positions enabled child Objects horizontally each update tick. Children
* are placed left-to-right starting at this Object's pos, separated by
* `spacing` pixels. Layout queries each child's intrinsic size via the
* shared widgetSize() dispatch so any 2D widget composes naturally.
*
* Children get pos.x rewritten in canvas space; their pos.y is set to this
* Object's pos.y plus an alignment offset (top, center, bottom).
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct HBoxLayout
  {
    static constexpr uint32_t ID = 21;

    int16_t spacing{2};
    uint8_t alignY{0};   // 0 = top, 1 = center, 2 = bottom
    uint8_t _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(HBoxLayout);
    }

    static void initDelete(Object& obj, HBoxLayout* data, void* initData);
    static void update(Object& obj, HBoxLayout* data, float deltaTime);
    static void draw([[maybe_unused]] Object& obj, [[maybe_unused]] HBoxLayout* data,
                     [[maybe_unused]] float deltaTime) {}
  };
}
