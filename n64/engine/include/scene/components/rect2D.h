/**
* Rect2D component.
* Filled or outlined screen-space rectangle in solid color. Treats
* obj.pos.x/y as the top-left pixel position; width/height are component
* pixels. Implemented as either a single rdpq_fill_rectangle (when
* outlineThickness == 0) or four thin fills for the border edges.
*
* Companion to Panel2D, but minimal: no sprite path, no tint slot, no
* alpha-threshold. The use case is rect chrome (Pixic's menu borders,
* board frame, settings-row highlight) where you just want a flat shape.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Rect2D
  {
    static constexpr uint32_t ID = 24;

    uint16_t width{32};
    uint16_t height{32};
    uint8_t  r{255};
    uint8_t  g{255};
    uint8_t  b{255};
    uint8_t  a{255};
    uint8_t  outlineThickness{0};   // 0 = filled. >0 = outline px.
    uint8_t  _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Rect2D);
    }

    static void initDelete(Object& obj, Rect2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Rect2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Rect2D* data, float deltaTime);
  };
}
