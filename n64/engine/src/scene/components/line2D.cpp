/**
* Line2D implementation.
* Axis-aligned lines compile to a single rdpq_fill_rectangle; diagonal
* lines fall back to Bresenham with one tiny fill per row of the major
* axis. The Bresenham path is only used when neither dx nor dy is zero,
* and the per-step fill is `thickness` wide so thickness > 1 still looks
* uniform.
*/
#include "scene/components/line2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"

#include <stdlib.h>

namespace
{
  struct __attribute__((packed)) InitData
  {
    int16_t  dx;
    int16_t  dy;
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
    uint8_t  a;
    uint8_t  thickness;
    uint8_t  _pad0;
    uint8_t  _pad1;
    uint8_t  _pad2;
  };
  static_assert(sizeof(InitData) == 12);

  inline int iabs(int v) { return v < 0 ? -v : v; }
}

namespace P64::Comp
{
  void Line2D::initDelete(Object &obj, Line2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;

    data->dx        = src->dx;
    data->dy        = src->dy;
    data->r         = src->r;
    data->g         = src->g;
    data->b         = src->b;
    data->a         = src->a;
    data->thickness = src->thickness ? src->thickness : 1;
    (void)obj;
  }

  void Line2D::draw(Object &obj, Line2D* data, [[maybe_unused]] float deltaTime)
  {
    int x0 = (int)obj.pos.x;
    int y0 = (int)obj.pos.y;
    int x1 = x0 + (int)data->dx;
    int y1 = y0 + (int)data->dy;
    int t  = data->thickness < 1 ? 1 : (int)data->thickness;

    rdpq_set_mode_fill({data->r, data->g, data->b, data->a});

    int dx = x1 - x0;
    int dy = y1 - y0;

    if (dy == 0) {
      int xa = dx >= 0 ? x0 : x1;
      int xb = dx >= 0 ? x1 : x0;
      rdpq_fill_rectangle(xa, y0, xb + 1, y0 + t);
      return;
    }
    if (dx == 0) {
      int ya = dy >= 0 ? y0 : y1;
      int yb = dy >= 0 ? y1 : y0;
      rdpq_fill_rectangle(x0, ya, x0 + t, yb + 1);
      return;
    }

    // Bresenham, walking along the major axis so each output is one
    // tiny rect (thickness x 1 or 1 x thickness depending on slope).
    int adx = iabs(dx);
    int ady = iabs(dy);
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    int x = x0, y = y0;
    if (adx >= ady) {
      int err = adx / 2;
      for (int i = 0; i <= adx; i++) {
        rdpq_fill_rectangle(x, y, x + 1, y + t);
        err -= ady;
        if (err < 0) { y += sy; err += adx; }
        x += sx;
      }
    } else {
      int err = ady / 2;
      for (int i = 0; i <= ady; i++) {
        rdpq_fill_rectangle(x, y, x + t, y + 1);
        err -= adx;
        if (err < 0) { x += sx; err += ady; }
        y += sy;
      }
    }
  }
}
