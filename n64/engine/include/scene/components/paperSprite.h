/**
* PaperSprite component.
* Depth-tested sibling of SpriteBillboard. The whole sprite is rasterized at a
* single z (the world anchor's projected depth) with z-test and z-write on, so
* 3D geometry occludes / is occluded by it as if it were a flat 2D cutout
* standing at the anchor. Data layout matches SpriteBillboard byte-for-byte
* on purpose so the editor InitData is the same shape.
*/
#pragma once
#include <libdragon.h>
#include "assets/assetManager.h"
#include "scene/object.h"

namespace P64::Comp
{
  struct PaperSprite
  {
    static constexpr uint32_t ID = 17;

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
      return sizeof(PaperSprite);
    }

    static void initDelete(Object& obj, PaperSprite* data, void* initData);
    static void update([[maybe_unused]] Object& obj, [[maybe_unused]] PaperSprite* data,
                       [[maybe_unused]] float deltaTime) {}
    static void draw(Object& obj, PaperSprite* data, float deltaTime);
  };
}
