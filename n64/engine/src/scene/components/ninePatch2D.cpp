/**
* NinePatch2D implementation.
* Nine rdpq_sprite_blit calls into the source sprite's UV regions, scaled to
* fit the destination rect. We compute destination spans by subtracting the
* four border insets from the requested width/height; if the result would
* be negative the edge/center collapses (corners always render).
*/
#include "scene/components/ninePatch2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Layout matches compNinePatch2D.cpp::build (sizeof == 16).
  struct __attribute__((packed)) InitData
  {
    uint16_t assetIdx;       // 0xFFFF = no sprite, draw nothing
    uint16_t width;
    uint16_t height;
    uint8_t  borderL;
    uint8_t  borderR;
    uint8_t  borderT;
    uint8_t  borderB;
    uint8_t  tintR;
    uint8_t  tintG;
    uint8_t  tintB;
    uint8_t  tintA;
    uint8_t  alphaThreshold;
    uint8_t  _pad;
  };
  static_assert(sizeof(InitData) == 16);

  // Single 9-patch slice. s0/t0 + sw/sh select the source UV; dx/dy + dw/dh
  // is the destination rect. Scale collapses to 1.0 for fixed corners.
  inline void blitSlice(sprite_t *spr,
                        int s0, int t0, int sw, int sh,
                        int dx, int dy, int dw, int dh)
  {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    rdpq_blitparms_t parms{};
    parms.s0      = s0;
    parms.t0      = t0;
    parms.width   = sw;
    parms.height  = sh;
    parms.scale_x = (float)dw / (float)sw;
    parms.scale_y = (float)dh / (float)sh;
    rdpq_sprite_blit(spr, dx, dy, &parms);
  }
}

namespace P64::Comp
{
  void NinePatch2D::initDelete(Object &obj, NinePatch2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;

    data->width   = src->width  ? src->width  : 1;
    data->height  = src->height ? src->height : 1;
    data->borderL = src->borderL;
    data->borderR = src->borderR;
    data->borderT = src->borderT;
    data->borderB = src->borderB;
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

  void NinePatch2D::draw(Object &obj, NinePatch2D* data, [[maybe_unused]] float deltaTime)
  {
    sprite_t *spr = data->sprite;
    if (!spr) return;

    int sw = spr->width;
    int sh = spr->height;
    int bL = data->borderL, bR = data->borderR;
    int bT = data->borderT, bB = data->borderB;

    // Clamp borders so corners cannot exceed source dimensions.
    if (bL + bR > sw) { bL = sw / 2; bR = sw - bL; }
    if (bT + bB > sh) { bT = sh / 2; bB = sh - bT; }

    int srcMidW = sw - bL - bR;
    int srcMidH = sh - bT - bB;
    if (srcMidW < 0) srcMidW = 0;
    if (srcMidH < 0) srcMidH = 0;

    int x = (int)obj.pos.x;
    int y = (int)obj.pos.y;
    int w = (int)data->width;
    int h = (int)data->height;

    int dstMidW = w - bL - bR;
    int dstMidH = h - bT - bB;
    if (dstMidW < 0) dstMidW = 0;
    if (dstMidH < 0) dstMidH = 0;

    DrawLayer::beginSprite2D(DrawLayer::Blend2D::Alpha, false, data->alphaThreshold);
    rdpq_set_prim_color({data->tintR, data->tintG, data->tintB, data->tintA});

    int xL = x;
    int xM = x + bL;
    int xR = x + bL + dstMidW;
    int yT = y;
    int yM = y + bT;
    int yB = y + bT + dstMidH;

    blitSlice(spr,         0,         0, bL,      bT,      xL, yT, bL,      bT);
    blitSlice(spr,        bL,         0, srcMidW, bT,      xM, yT, dstMidW, bT);
    blitSlice(spr, sw - bR,           0, bR,      bT,      xR, yT, bR,      bT);

    blitSlice(spr,         0,        bT, bL,      srcMidH, xL, yM, bL,      dstMidH);
    blitSlice(spr,        bL,        bT, srcMidW, srcMidH, xM, yM, dstMidW, dstMidH);
    blitSlice(spr, sw - bR,          bT, bR,      srcMidH, xR, yM, bR,      dstMidH);

    blitSlice(spr,         0, sh - bB,   bL,      bB,      xL, yB, bL,      bB);
    blitSlice(spr,        bL, sh - bB,   srcMidW, bB,      xM, yB, dstMidW, bB);
    blitSlice(spr, sw - bR,   sh - bB,   bR,      bB,      xR, yB, bR,      bB);
  }
}
