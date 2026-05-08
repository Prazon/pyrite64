/**
* ProgressBar2D implementation.
* Two filled rdpq rectangles. value is normalized 0..1 and clamped here.
* Authoring tools and game code can both write `value` directly.
*/
#include "scene/components/progressBar2D.h"
#include "scene/scene.h"

namespace
{
  // Layout matches compProgressBar2D.cpp::build. Default-initialized values
  // come from the editor; runtime mutations to `value` after init don't
  // need a re-init, they're just a normal field write.
  struct __attribute__((packed)) InitData
  {
    uint16_t width;
    uint16_t height;
    float    value;
    uint8_t  bgR;
    uint8_t  bgG;
    uint8_t  bgB;
    uint8_t  bgA;
    uint8_t  fgR;
    uint8_t  fgG;
    uint8_t  fgB;
    uint8_t  fgA;
    uint8_t  _pad0;
    uint8_t  _pad1;
    uint8_t  _pad2;
    uint8_t  _pad3;
  };
  static_assert(sizeof(InitData) == 20);
}

namespace P64::Comp
{
  void ProgressBar2D::initDelete(Object &obj, ProgressBar2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;
    data->width  = src->width  ? src->width  : 64;
    data->height = src->height ? src->height : 8;
    data->value  = src->value;
    data->bgR = src->bgR;
    data->bgG = src->bgG;
    data->bgB = src->bgB;
    data->bgA = src->bgA;
    data->fgR = src->fgR;
    data->fgG = src->fgG;
    data->fgB = src->fgB;
    data->fgA = src->fgA;
    (void)obj;
  }

  void ProgressBar2D::draw(Object &obj, ProgressBar2D* data, [[maybe_unused]] float deltaTime)
  {
    int x = (int)obj.pos.x;
    int y = (int)obj.pos.y;
    int w = (int)data->width;
    int h = (int)data->height;

    float v = data->value;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int fillW = (int)((float)w * v);

    rdpq_set_mode_fill({data->bgR, data->bgG, data->bgB, data->bgA});
    rdpq_fill_rectangle(x, y, x + w, y + h);

    if (fillW > 0) {
      rdpq_set_mode_fill({data->fgR, data->fgG, data->fgB, data->fgA});
      rdpq_fill_rectangle(x, y, x + fillW, y + h);
    }
  }
}
