/**
* Grid2D component.
* N x M cell grid with a tileset sprite. Each cell stores a uint8 value:
* 0 = empty, 1..255 = frame index into the tileset (1-based so the
* default zero-init means "empty"). Per-cell shake counters add a random
* +/- pixel offset to a cell's draw position for `shakeFrames` frames
* after Grid2D::shake() is called.
*
* obj.pos.x/y is the grid's top-left corner. Cells are cellW x cellH
* pixels each; the tileset sprite is treated as a sheet whose column
* count is (tileset.width / cellW), mirroring Sprite2D::frame layout.
*
* Cells and shake counters are heap-allocated from init data carrying
* the configured width/height so a single component instance can host
* an 8x8 board (Pixic) or anything else up to 64x64.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Grid2D
  {
    static constexpr uint32_t ID = 27;

    sprite_t *tileset{nullptr};
    uint8_t  *cells{nullptr};       // width*height bytes
    uint8_t  *shakeFrames{nullptr}; // width*height bytes
    uint16_t  width{8};
    uint16_t  height{8};
    uint16_t  cellW{12};
    uint16_t  cellH{12};
    uint8_t   shakeMagnitude{1};    // pixel amplitude when shake is active
    uint8_t   alphaThreshold{0};
    uint8_t   _pad0{0};
    uint8_t   _pad1{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Grid2D);
    }

    static void initDelete(Object& obj, Grid2D* data, void* initData);
    static void update(Object& obj, Grid2D* data, float deltaTime);
    static void draw(Object& obj, Grid2D* data, float deltaTime);

    // 1-based frame index. 0 == empty. Bounds-checked.
    static void   setCell(Grid2D* data, int x, int y, uint8_t value);
    static uint8_t getCell(Grid2D* data, int x, int y);

    // Mark a cell as shaking for the next N update ticks.
    static void shake(Grid2D* data, int x, int y, uint8_t frames);
    static void clearShake(Grid2D* data);
  };
}
