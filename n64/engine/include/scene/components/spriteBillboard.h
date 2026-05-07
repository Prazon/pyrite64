/**
* SpriteBillboard component (added by SPBF64 fork)
* Renders a 2D sprite at the object's world position using rdpq_sprite_blit,
* with the world point projected through the active camera.
* Mirrors SPBF64's `render_sprite` from src/render.c — pixel-perfect 2D-in-3D.
*/
#pragma once
#include <libdragon.h>
#include "assets/assetManager.h"
#include "scene/object.h"

namespace P64::Comp
{
  struct SpriteBillboard
  {
    static constexpr uint32_t ID = 12;

    sprite_t *sprite{nullptr};
    uint16_t  cellW{0};        // 0 = use full sprite width
    uint16_t  cellH{0};        // 0 = use full sprite height
    uint16_t  frame{0};        // cell index for sprite-sheet animation
    int16_t   pivotX{0};       // pixel offset within cell that anchors at world pos (X)
    int16_t   pivotY{0};       // ... (Y); typical: cellW/2, cellH (feet anchor)
    uint8_t   flipX{0};
    uint8_t   layerIdx{0};
    uint8_t   pixelScaleQ{16}; // fixed-point scale, 16 = 1.0x; 0 = auto from camera
    uint8_t   alphaThreshold{100};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(SpriteBillboard);
    }

    static void initDelete(Object& obj, SpriteBillboard* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] SpriteBillboard* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, SpriteBillboard* data, float deltaTime);
  };
}
