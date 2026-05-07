/**
* SpriteBillboard component implementation (added by SPBF64 fork).
* Matches SPBF64's render_sprite() approach: project world position via active
* camera, then rdpq_sprite_blit to draw a 2D sprite at that screen position.
*/
#include "scene/components/spriteBillboard.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Binary layout matching what compSpriteBillboard.cpp::build writes.
  // Keep field order/padding in sync with the editor side.
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
  void SpriteBillboard::initDelete(Object &obj, SpriteBillboard* data, void* initData_)
  {
    auto initData = (InitData*)initData_;
    if (initData == nullptr) {
      // destroy: nothing to free (assets are managed globally)
      return;
    }

    // initialize via placement-new on packed POD-ish struct
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

  void SpriteBillboard::draw(Object &obj, SpriteBillboard* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->sprite) return;

    auto &cam = SceneManager::getCurrent().getActiveCamera();

    // Determine cell dimensions (default to full sprite)
    int cellW = data->cellW ? data->cellW : data->sprite->width;
    int cellH = data->cellH ? data->cellH : data->sprite->height;
    int sheetCols = data->cellW ? (data->sprite->width / data->cellW) : 1;
    if (sheetCols < 1) sheetCols = 1;

    // Project world position to screen
    fm_vec3_t world = obj.pos;
    fm_vec3_t scrFeet = cam.getScreenPos(world);

    // Auto-scale: project a unit-height world segment and compare to cell pixel height
    float scale = (data->pixelScaleQ != 0)
                    ? (float)data->pixelScaleQ * (1.0f / 16.0f)
                    : 1.0f;
    if (data->pixelScaleQ == 0) {
      // Auto: use the camera-distance heuristic — measure projection of (worldY+1)
      fm_vec3_t worldTop = world; worldTop.y += (float)cellH;
      fm_vec3_t scrTop = cam.getScreenPos(worldTop);
      float projHeight = scrFeet.y - scrTop.y;
      if (projHeight > 0.5f) {
        scale = projHeight / (float)cellH;
      }
      // Snap to clean integer scales for crisp pixel art
      if (scale > 0.95f && scale < 1.05f) scale = 1.0f;
      else if (scale > 1.95f && scale < 2.05f) scale = 2.0f;
      else if (scale > 0.45f && scale < 0.55f) scale = 0.5f;
      if (scale < 0.1f) scale = 0.1f;
    }

    // Sprite-sheet cell offset in source texture
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

    // Anchor: scrFeet is the world-space anchor; pivot is the pixel offset within
    // the sprite cell that should land on that anchor. Default pivot (0,0) maps
    // top-left of cell to the world position.
    int drawX = (int)scrFeet.x - (int)((float)data->pivotX * scale);
    int drawY = (int)scrFeet.y - (int)((float)data->pivotY * scale);

    if (data->layerIdx) DrawLayer::use2D(data->layerIdx);

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(data->alphaThreshold);
    rdpq_sprite_blit(data->sprite, drawX, drawY, &parms);

    if (data->layerIdx) DrawLayer::useDefault();
  }
}
