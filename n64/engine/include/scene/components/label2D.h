/**
* Label2D component.
* Screen-space text rendered with rdpq_text_print. Treats obj.pos.x/y as
* pixel coords. Font is selected by slot index — same slot space used by
* main.cpp's auto-load loop (rdpq_text_register_font).
*
* The text payload follows the fixed-size InitData struct in the scene file
* and is copied into a heap allocation owned by this component during
* initDelete; freed when the component is torn down.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Label2D
  {
    static constexpr uint32_t ID = 15;

    char    *text{nullptr};      // owned heap copy, NUL-terminated
    uint8_t  fontSlot{0};        // index into rdpq_text font registry
    uint8_t  styleId{0};         // optional rdpq_text style index (0 = default)
    uint8_t  colorR{255};
    uint8_t  colorG{255};
    uint8_t  colorB{255};
    uint8_t  colorA{255};
    uint8_t  _pad0{0};
    uint8_t  _pad1{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Label2D);
    }

    static void initDelete(Object& obj, Label2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Label2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Label2D* data, float deltaTime);
  };
}
