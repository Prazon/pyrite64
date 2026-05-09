/**
* Button2D component.
* Hit-region + 3 sprite slots (normal / hover / pressed) with an event
* dispatched on A-press while focused. N64 has no mouse, so "hover"
* visually is whatever WidgetFocus has put focus on; "pressed" appears for
* the single frame the A button transitions down. The dispatched event
* type is stored in `eventType`; user scripts subscribe via the existing
* event bus.
*
* Width/height are explicit so layout containers can lay buttons out
* without having to inspect the sprite assets. obj.pos.x/y is the top-left
* pixel of the hit-region in canvas coords.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Button2D
  {
    static constexpr uint32_t ID = 23;

    sprite_t *spriteNormal{nullptr};
    sprite_t *spriteFocus{nullptr};
    sprite_t *spritePress{nullptr};

    uint16_t width{32};
    uint16_t height{16};
    uint16_t eventType{0};   // dispatched to obj on A-press while focused
    uint8_t  initialFocus{0};
    uint8_t  alphaThreshold{1};
    uint8_t  tintR{255};
    uint8_t  tintG{255};
    uint8_t  tintB{255};
    uint8_t  tintA{255};

    // Runtime state. Not serialized; reset every frame in update().
    uint8_t  isPressed{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Button2D);
    }

    static void initDelete(Object& obj, Button2D* data, void* initData);
    static void update(Object& obj, Button2D* data, float deltaTime);
    static void draw(Object& obj, Button2D* data, float deltaTime);
  };
}
