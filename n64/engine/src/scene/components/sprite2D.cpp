/**
* Sprite2D component implementation.
* Treats obj.pos.x/y as pixel coordinates in the framebuffer; emits an
* rdpq_sprite_blit at that position. No camera projection — the parent
* scene.cpp 2D pass has already swapped to the screen-space rspq queue.
*/
#include "scene/components/sprite2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Layout matches what compSprite2D.cpp::build writes. Keep field order
  // and padding aligned with the editor side or scenes will deserialize
  // garbage.
  struct __attribute__((packed)) InitData
  {
    uint16_t assetIdx;       // 0xFFFF = no sprite assigned
    uint16_t cellW;
    uint16_t cellH;
    uint16_t frame;
    uint8_t  flipX;
    uint8_t  alphaThreshold;
    uint8_t  tintR;
    uint8_t  tintG;
    uint8_t  tintB;
    uint8_t  tintA;
    uint8_t  pixelScaleQ;
    uint8_t  _pad;
  };
  static_assert(sizeof(InitData) == 16);
}

namespace P64::Comp
{
  void Sprite2D::initDelete(Object &obj, Sprite2D* data, void* initData_)
  {
    auto initData = (InitData*)initData_;
    if (initData == nullptr) return; // destroy: no owned heap to free

    data->cellW          = initData->cellW;
    data->cellH          = initData->cellH;
    data->frame          = initData->frame;
    data->flipX          = initData->flipX;
    data->alphaThreshold = initData->alphaThreshold;
    data->tintR          = initData->tintR;
    data->tintG          = initData->tintG;
    data->tintB          = initData->tintB;
    data->tintA          = initData->tintA;
    data->pixelScaleQ    = initData->pixelScaleQ ? initData->pixelScaleQ : 16;

    if (initData->assetIdx == 0xFFFF) {
      data->sprite = nullptr;
    } else {
      data->sprite = (sprite_t*)AssetManager::getByIndex(initData->assetIdx);
    }
    (void)obj;
  }

  void Sprite2D::draw(Object &obj, Sprite2D* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->sprite) return;

    int cellW = data->cellW ? data->cellW : data->sprite->width;
    int cellH = data->cellH ? data->cellH : data->sprite->height;
    int sheetCols = data->cellW ? (data->sprite->width / data->cellW) : 1;
    if (sheetCols < 1) sheetCols = 1;

    int s0 = (data->frame % sheetCols) * cellW;
    int t0 = (data->frame / sheetCols) * cellH;

    float scale = (float)data->pixelScaleQ * (1.0f / 16.0f);
    if (scale <= 0.0f) scale = 1.0f;

    rdpq_blitparms_t parms{};
    parms.s0       = s0;
    parms.t0       = t0;
    parms.width    = cellW;
    parms.height   = cellH;
    parms.scale_x  = scale;
    parms.scale_y  = scale;
    parms.flip_x   = (data->flipX != 0);

    int drawX = (int)obj.pos.x;
    int drawY = (int)obj.pos.y;

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(data->alphaThreshold);
    rdpq_set_prim_color({data->tintR, data->tintG, data->tintB, data->tintA});
    rdpq_sprite_blit(data->sprite, drawX, drawY, &parms);
  }
}
