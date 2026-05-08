/**
* ProgressBar2D component.
* Screen-space rectangle pair (background + fill) for HP / score / charge
* meters. Treats obj.pos.x/y as the top-left pixel position; width and
* height are component-local in pixels.
*
* The fill rect width is `width * clamp(value, 0, 1)`. value defaults to 1.
* Game code mutates it at runtime by calling getComponent<ProgressBar2D>()
* and writing the `value` field; no event is required.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct ProgressBar2D
  {
    static constexpr uint32_t ID = 16;

    uint16_t width{64};
    uint16_t height{8};
    float    value{1.0f}; // 0.0 .. 1.0
    uint8_t  bgR{32};
    uint8_t  bgG{32};
    uint8_t  bgB{32};
    uint8_t  bgA{255};
    uint8_t  fgR{220};
    uint8_t  fgG{50};
    uint8_t  fgB{50};
    uint8_t  fgA{255};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(ProgressBar2D);
    }

    static void initDelete(Object& obj, ProgressBar2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] ProgressBar2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, ProgressBar2D* data, float deltaTime);
  };
}
