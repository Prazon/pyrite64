/**
* VBoxLayout component.
* Vertical sibling of HBoxLayout. Positions children top-to-bottom starting
* at this Object's pos, separated by `spacing` pixels. alignX selects the
* horizontal placement (left, center, right) for children narrower than
* the widest one in the column.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct VBoxLayout
  {
    static constexpr uint32_t ID = 22;

    int16_t spacing{2};
    uint8_t alignX{0};   // 0 = left, 1 = center, 2 = right
    uint8_t _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(VBoxLayout);
    }

    static void initDelete(Object& obj, VBoxLayout* data, void* initData);
    static void update(Object& obj, VBoxLayout* data, float deltaTime);
    static void draw([[maybe_unused]] Object& obj, [[maybe_unused]] VBoxLayout* data,
                     [[maybe_unused]] float deltaTime) {}
  };
}
