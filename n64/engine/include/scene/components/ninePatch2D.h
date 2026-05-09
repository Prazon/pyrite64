/**
* NinePatch2D component.
* Sprite-bordered resizable panel. The source sprite is divided into nine
* regions by four border insets (left/right/top/bottom in source pixels).
* Corners are drawn at fixed size; edges and center stretch to fill the
* requested width x height. Used for window/dialog frames where the corners
* should stay sharp regardless of panel size.
*
* obj.pos.x/y is the top-left pixel of the destination rect. width/height
* are the destination rect size in pixels.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct NinePatch2D
  {
    static constexpr uint32_t ID = 20;

    sprite_t *sprite{nullptr};
    uint16_t width{32};
    uint16_t height{32};
    uint8_t  borderL{4};
    uint8_t  borderR{4};
    uint8_t  borderT{4};
    uint8_t  borderB{4};
    uint8_t  tintR{255};
    uint8_t  tintG{255};
    uint8_t  tintB{255};
    uint8_t  tintA{255};
    uint8_t  alphaThreshold{1};
    uint8_t  _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(NinePatch2D);
    }

    static void initDelete(Object& obj, NinePatch2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] NinePatch2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, NinePatch2D* data, float deltaTime);
  };
}
