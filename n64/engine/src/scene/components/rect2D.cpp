/**
* Rect2D implementation.
* Two render paths: rdpq_fill_rectangle for the filled case, four rect
* fills for the outlined case. Both run on the standard pipeline; no
* texture upload, no TMEM cost.
*/
#include "scene/components/rect2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"

namespace
{
  struct __attribute__((packed)) InitData
  {
    uint16_t width;
    uint16_t height;
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
    uint8_t  a;
    uint8_t  outlineThickness;
    uint8_t  _pad;
  };
  static_assert(sizeof(InitData) == 10);
}

namespace P64::Comp
{
  void Rect2D::initDelete(Object &obj, Rect2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;

    data->width            = src->width  ? src->width  : 1;
    data->height           = src->height ? src->height : 1;
    data->r                = src->r;
    data->g                = src->g;
    data->b                = src->b;
    data->a                = src->a;
    data->outlineThickness = src->outlineThickness;
    (void)obj;
  }

  void Rect2D::draw(Object &obj, Rect2D* data, [[maybe_unused]] float deltaTime)
  {
    int x = (int)obj.pos.x;
    int y = (int)obj.pos.y;
    int w = (int)data->width;
    int h = (int)data->height;
    if (w <= 0 || h <= 0) return;

    rdpq_set_mode_fill({data->r, data->g, data->b, data->a});

    if (data->outlineThickness == 0) {
      rdpq_fill_rectangle(x, y, x + w, y + h);
      return;
    }

    int t = (int)data->outlineThickness;
    // Clamp thickness so a thick outline on a tiny rect collapses to fill.
    if (t * 2 >= w || t * 2 >= h) {
      rdpq_fill_rectangle(x, y, x + w, y + h);
      return;
    }

    // Top strip, bottom strip, left pillar (interior), right pillar (interior).
    rdpq_fill_rectangle(x,        y,         x + w,     y + t);
    rdpq_fill_rectangle(x,        y + h - t, x + w,     y + h);
    rdpq_fill_rectangle(x,        y + t,     x + t,     y + h - t);
    rdpq_fill_rectangle(x + w - t,y + t,     x + w,     y + h - t);
  }
}
