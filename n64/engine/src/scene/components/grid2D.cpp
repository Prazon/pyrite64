/**
* Grid2D implementation.
* update() decrements all positive shake counters by 1 per tick (the
* counter is a frame budget, not a duration in seconds — keeps the API
* trivially callable from gameplay without time-budget math).
*
* draw() walks the grid in row-major order. For each non-zero cell it
* maps the 1-based value back to a 0-based sprite-sheet frame and emits
* one rdpq_sprite_blit. Shake offsets are computed inline from
* rand()-ish per-call values so the visual jitter doesn't depend on
* the engine's RNG state.
*/
#include "scene/components/grid2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

#include <malloc.h>
#include <string.h>
#include <stdlib.h>

namespace
{
  struct __attribute__((packed)) InitData
  {
    uint16_t tilesetIdx;      // 0xFFFF = no tileset (draws nothing)
    uint16_t width;
    uint16_t height;
    uint16_t cellW;
    uint16_t cellH;
    uint8_t  shakeMagnitude;
    uint8_t  alphaThreshold;
    uint8_t  _pad0;
    uint8_t  _pad1;
  };
  static_assert(sizeof(InitData) == 16);

  inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
}

namespace P64::Comp
{
  void Grid2D::initDelete(Object &obj, Grid2D* data, void* initData_)
  {
    if (initData_ == nullptr) {
      // destroy
      if (data->cells)       { free(data->cells);       data->cells = nullptr; }
      if (data->shakeFrames) { free(data->shakeFrames); data->shakeFrames = nullptr; }
      return;
    }

    auto *src = (InitData*)initData_;
    data->width          = src->width  ? src->width  : 1;
    data->height         = src->height ? src->height : 1;
    data->cellW          = src->cellW  ? src->cellW  : 8;
    data->cellH          = src->cellH  ? src->cellH  : 8;
    data->shakeMagnitude = src->shakeMagnitude;
    data->alphaThreshold = src->alphaThreshold;

    size_t cellCount = (size_t)data->width * data->height;
    data->cells       = (uint8_t*)malloc(cellCount);
    data->shakeFrames = (uint8_t*)malloc(cellCount);
    if (data->cells)       memset(data->cells, 0, cellCount);
    if (data->shakeFrames) memset(data->shakeFrames, 0, cellCount);

    data->tileset = (src->tilesetIdx == 0xFFFF)
      ? nullptr
      : (sprite_t*)AssetManager::getByIndex(src->tilesetIdx);
    (void)obj;
  }

  void Grid2D::setCell(Grid2D* data, int x, int y, uint8_t value)
  {
    if (!data || !data->cells) return;
    if (x < 0 || x >= (int)data->width)  return;
    if (y < 0 || y >= (int)data->height) return;
    data->cells[y * data->width + x] = value;
  }

  uint8_t Grid2D::getCell(Grid2D* data, int x, int y)
  {
    if (!data || !data->cells) return 0;
    if (x < 0 || x >= (int)data->width)  return 0;
    if (y < 0 || y >= (int)data->height) return 0;
    return data->cells[y * data->width + x];
  }

  void Grid2D::shake(Grid2D* data, int x, int y, uint8_t frames)
  {
    if (!data || !data->shakeFrames) return;
    if (x < 0 || x >= (int)data->width)  return;
    if (y < 0 || y >= (int)data->height) return;
    data->shakeFrames[y * data->width + x] = frames;
  }

  void Grid2D::clearShake(Grid2D* data)
  {
    if (!data || !data->shakeFrames) return;
    size_t n = (size_t)data->width * data->height;
    memset(data->shakeFrames, 0, n);
  }

  void Grid2D::update([[maybe_unused]] Object &obj, Grid2D* data,
                     [[maybe_unused]] float deltaTime)
  {
    if (!data->shakeFrames) return;
    size_t n = (size_t)data->width * data->height;
    for (size_t i = 0; i < n; i++) {
      if (data->shakeFrames[i] > 0) data->shakeFrames[i]--;
    }
  }

  void Grid2D::draw(Object &obj, Grid2D* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->tileset || !data->cells) return;

    int sheetCols = (data->cellW > 0)
                      ? (data->tileset->width / data->cellW)
                      : 1;
    if (sheetCols < 1) sheetCols = 1;

    int ox = (int)obj.pos.x;
    int oy = (int)obj.pos.y;
    int cw = data->cellW;
    int ch = data->cellH;
    int mag = (int)data->shakeMagnitude;

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    if (data->alphaThreshold > 0) {
      rdpq_mode_alphacompare(data->alphaThreshold);
    }
    rdpq_set_prim_color({255, 255, 255, 255});

    for (int y = 0; y < (int)data->height; y++) {
      for (int x = 0; x < (int)data->width; x++) {
        uint8_t v = data->cells[y * data->width + x];
        if (v == 0) continue;
        int frame = (int)v - 1;
        int s0 = (frame % sheetCols) * cw;
        int t0 = (frame / sheetCols) * ch;

        int dx = ox + x * cw;
        int dy = oy + y * ch;
        if (data->shakeFrames && data->shakeFrames[y * data->width + x] > 0 && mag > 0) {
          int sr = rand();
          dx += (sr & 1)        ? mag : -mag;
          dy += ((sr >> 1) & 1) ? mag : -mag;
        }

        rdpq_blitparms_t parms{};
        parms.s0     = s0;
        parms.t0     = t0;
        parms.width  = cw;
        parms.height = ch;
        rdpq_sprite_blit(data->tileset, dx, dy, &parms);
      }
    }
  }
}
