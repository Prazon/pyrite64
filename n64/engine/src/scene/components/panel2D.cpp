/**
* Panel2D implementation.
* Either rdpq_fill_rectangle (solid mode) or a single rdpq_sprite_blit
* stretched to the panel size. The runtime branch on `sprite` is cheap and
* leaves both code paths obvious.
*/
#include "scene/components/panel2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Layout matches compPanel2D.cpp::build (sizeof == 16).
  struct __attribute__((packed)) InitData
  {
    uint16_t assetIdx;       // 0xFFFF = solid fill, no sprite
    uint16_t width;
    uint16_t height;
    uint8_t  fillR;
    uint8_t  fillG;
    uint8_t  fillB;
    uint8_t  fillA;
    uint8_t  tintR;
    uint8_t  tintG;
    uint8_t  tintB;
    uint8_t  tintA;
    uint8_t  alphaThreshold;
    uint8_t  _pad;
  };
  static_assert(sizeof(InitData) == 16);
}

namespace P64::Comp
{
  void Panel2D::initDelete(Object &obj, Panel2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;

    data->width  = src->width  ? src->width  : 1;
    data->height = src->height ? src->height : 1;
    data->fillR = src->fillR;
    data->fillG = src->fillG;
    data->fillB = src->fillB;
    data->fillA = src->fillA;
    data->tintR = src->tintR;
    data->tintG = src->tintG;
    data->tintB = src->tintB;
    data->tintA = src->tintA;
    data->alphaThreshold = src->alphaThreshold;

    if (src->assetIdx == 0xFFFF) {
      data->sprite = nullptr;
    } else {
      data->sprite = (sprite_t*)AssetManager::getByIndex(src->assetIdx);
    }
    (void)obj;
  }

  void Panel2D::draw(Object &obj, Panel2D* data, [[maybe_unused]] float deltaTime)
  {
    int x = (int)obj.pos.x;
    int y = (int)obj.pos.y;
    int w = (int)data->width;
    int h = (int)data->height;

    if (data->sprite) {
      rdpq_blitparms_t parms{};
      parms.width   = data->sprite->width;
      parms.height  = data->sprite->height;
      parms.scale_x = (data->sprite->width  > 0) ? ((float)w / (float)data->sprite->width)  : 1.0f;
      parms.scale_y = (data->sprite->height > 0) ? ((float)h / (float)data->sprite->height) : 1.0f;

      DrawLayer::beginSprite2D(DrawLayer::Blend2D::Alpha, false, data->alphaThreshold);
      rdpq_set_prim_color({data->tintR, data->tintG, data->tintB, data->tintA});
      rdpq_sprite_blit(data->sprite, x, y, &parms);
      return;
    }

    rdpq_set_mode_fill({data->fillR, data->fillG, data->fillB, data->fillA});
    rdpq_fill_rectangle(x, y, x + w, y + h);
  }
}
