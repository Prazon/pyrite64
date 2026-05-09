/**
* Panel2D component.
* Solid-color (or single-sprite) screen-space rectangle. Used as a fill
* background for HUD groups, dialog body, modal overlay, etc.
*
* Treats obj.pos.x/y as the top-left pixel position. Width and height are
* component-local pixels. If `sprite` is non-null the sprite is blitted
* (stretched to width x height); otherwise the panel is drawn as a single
* rdpq_fill_rectangle with the configured fill RGBA.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Panel2D
  {
    static constexpr uint32_t ID = 19;

    sprite_t *sprite{nullptr};   // optional; null = solid fill
    uint16_t  width{64};
    uint16_t  height{32};
    uint8_t   fillR{20};
    uint8_t   fillG{20};
    uint8_t   fillB{20};
    uint8_t   fillA{200};
    uint8_t   tintR{255};
    uint8_t   tintG{255};
    uint8_t   tintB{255};
    uint8_t   tintA{255};
    uint8_t   alphaThreshold{0};
    uint8_t   _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Panel2D);
    }

    static void initDelete(Object& obj, Panel2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Panel2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Panel2D* data, float deltaTime);
  };
}
