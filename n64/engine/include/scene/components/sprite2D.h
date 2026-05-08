/**
* Sprite2D component.
* Screen-space sprite rendered during DrawLayer::use2D(). Companion to
* spriteBillboard.h, but with no camera projection — obj.pos.x/y is treated
* as raw pixel coordinates inside the framebuffer (320×240 by default).
*
* Component handlers run from the 2D-pass walk in scene.cpp; the parent
* Object must carry ObjectFlags::RENDER_LAYER_2D for that walk to reach it.
*/
#pragma once
#include <libdragon.h>
#include "assets/assetManager.h"
#include "scene/object.h"

namespace P64::Comp
{
  struct Sprite2D
  {
    static constexpr uint32_t ID = 14;

    sprite_t *sprite{nullptr};
    uint16_t cellW{0};       // 0 = use full sprite width
    uint16_t cellH{0};       // 0 = use full sprite height
    uint16_t frame{0};       // cell index for sprite-sheet animation
    uint8_t  flipX{0};
    uint8_t  alphaThreshold{100};
    uint8_t  tintR{255};
    uint8_t  tintG{255};
    uint8_t  tintB{255};
    uint8_t  tintA{255};
    uint8_t  pixelScaleQ{16}; // q4.4: 16 = 1.0x. 0 collapses to 1.0x.
    uint8_t  _pad{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Sprite2D);
    }

    static void initDelete(Object& obj, Sprite2D* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] Sprite2D* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, Sprite2D* data, float deltaTime);
  };
}
