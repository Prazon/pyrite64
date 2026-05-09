/**
* PaperSprite component implementation.
* Same projection / blit path as SpriteBillboard, but the entire sprite is
* drawn at a single fixed depth (the world anchor's NDC z, mapped to [0,1])
* with z-test and z-write enabled. That gives a flat 2D cutout that occludes
* and is occluded by 3D geometry at its anchor depth.
*/
#include "scene/components/paperSprite.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Binary layout matches compPaperSprite.cpp::build (which mirrors the
  // SpriteBillboard editor side). Keep field order/padding in sync with both.
  struct __attribute__((packed)) InitData
  {
    uint16_t assetIdx;        // sprite asset index (0xFFFF = none)
    uint16_t cellW;
    uint16_t cellH;
    uint16_t frame;
    int16_t  pivotX;
    int16_t  pivotY;
    uint8_t  flipX;
    uint8_t  layerIdx;
    uint8_t  pixelScaleQ;     // fixed q4.4: 16=1.0x; 0=auto from camera
    uint8_t  alphaThreshold;
  };
  static_assert(sizeof(InitData) == 16);
}

namespace P64::Comp
{
  void PaperSprite::initDelete(Object &obj, PaperSprite* data, void* initData_)
  {
    auto initData = (InitData*)initData_;
    if (initData == nullptr) {
      return;
    }

    data->cellW          = initData->cellW;
    data->cellH          = initData->cellH;
    data->frame          = initData->frame;
    data->pivotX         = initData->pivotX;
    data->pivotY         = initData->pivotY;
    data->flipX          = initData->flipX;
    data->layerIdx       = initData->layerIdx;
    data->pixelScaleQ    = initData->pixelScaleQ;
    data->alphaThreshold = initData->alphaThreshold;

    if (initData->assetIdx == 0xFFFF) {
      data->sprite = nullptr;
    } else {
      data->sprite = (sprite_t*)AssetManager::getByIndex(initData->assetIdx);
    }
    (void)obj;
  }

  void PaperSprite::draw(Object &obj, PaperSprite* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->sprite) return;

    auto &cam = SceneManager::getCurrent().getActiveCamera();

    int cellW = data->cellW ? data->cellW : data->sprite->width;
    int cellH = data->cellH ? data->cellH : data->sprite->height;
    int sheetCols = data->cellW ? (data->sprite->width / data->cellW) : 1;
    if (sheetCols < 1) sheetCols = 1;

    fm_vec3_t world = obj.pos;
    fm_vec3_t scrFeet = cam.getScreenPos(world);

    float scale = (data->pixelScaleQ != 0)
                    ? (float)data->pixelScaleQ * (1.0f / 16.0f)
                    : 1.0f;
    if (data->pixelScaleQ == 0) {
      fm_vec3_t worldTop = world; worldTop.y += (float)cellH;
      fm_vec3_t scrTop = cam.getScreenPos(worldTop);
      float projHeight = scrFeet.y - scrTop.y;
      if (projHeight > 0.5f) {
        scale = projHeight / (float)cellH;
      }
      if (scale > 0.95f && scale < 1.05f) scale = 1.0f;
      else if (scale > 1.95f && scale < 2.05f) scale = 2.0f;
      else if (scale > 0.45f && scale < 0.55f) scale = 0.5f;
      if (scale < 0.1f) scale = 0.1f;
    }

    int s0 = (data->frame % sheetCols) * cellW;
    int t0 = (data->frame / sheetCols) * cellH;

    rdpq_blitparms_t parms{};
    parms.s0       = s0;
    parms.t0       = t0;
    parms.width    = cellW;
    parms.height   = cellH;
    parms.scale_x  = scale;
    parms.scale_y  = scale;
    parms.flip_x   = (data->flipX != 0);

    int drawX = (int)scrFeet.x - (int)((float)data->pivotX * scale);
    int drawY = (int)scrFeet.y - (int)((float)data->pivotY * scale);

    // Map NDC z (~[-1,1]) to [0,1] for rdpq_mode_zoverride. Clamp so anchors
    // outside the frustum still produce a finite depth value rather than
    // wrapping past the 16-bit prim-z register.
    float z01 = scrFeet.z * 0.5f + 0.5f;
    if (z01 < 0.0f) z01 = 0.0f;
    if (z01 > 1.0f) z01 = 1.0f;

    if (data->layerIdx) DrawLayer::use2D(data->layerIdx);

    rdpq_set_mode_standard();
    rdpq_mode_begin();
      rdpq_mode_zbuf(true, true);
      rdpq_mode_zoverride(true, z01, 0);
      rdpq_mode_filter(FILTER_BILINEAR);
      rdpq_mode_alphacompare(data->alphaThreshold);
    rdpq_mode_end();
    rdpq_sprite_blit(data->sprite, drawX, drawY, &parms);

    if (data->layerIdx) DrawLayer::useDefault();
  }
}
