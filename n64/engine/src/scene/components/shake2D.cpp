/**
* Shake2D implementation.
*/
#include "scene/components/shake2D.h"

#include <stdlib.h>

namespace
{
  struct __attribute__((packed)) InitData
  {
    uint8_t defaultMagnitude;
    uint8_t _pad0;
    uint8_t _pad1;
    uint8_t _pad2;
  };
  static_assert(sizeof(InitData) == 4);
}

namespace P64::Comp
{
  void Shake2D::initDelete(Object &obj, Shake2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;
    data->magnitude  = src->defaultMagnitude ? src->defaultMagnitude : 1;
    data->framesLeft = 0;
    data->active     = 0;
    data->baselineX  = obj.pos.x;
    data->baselineY  = obj.pos.y;
  }

  void Shake2D::trigger(Object &obj, Shake2D* data, uint8_t magnitudePx, uint16_t frames)
  {
    if (!data) return;
    if (!data->active) {
      data->baselineX = obj.pos.x;
      data->baselineY = obj.pos.y;
    }
    data->magnitude  = magnitudePx ? magnitudePx : 1;
    data->framesLeft = frames;
    data->active     = frames ? 1 : 0;
    if (!data->active) {
      obj.pos.x = data->baselineX;
      obj.pos.y = data->baselineY;
    }
  }

  void Shake2D::stop(Object &obj, Shake2D* data)
  {
    if (!data || !data->active) return;
    obj.pos.x = data->baselineX;
    obj.pos.y = data->baselineY;
    data->framesLeft = 0;
    data->active     = 0;
  }

  void Shake2D::update(Object &obj, Shake2D* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->active) return;
    if (data->framesLeft == 0) {
      obj.pos.x = data->baselineX;
      obj.pos.y = data->baselineY;
      data->active = 0;
      return;
    }
    int mag = (int)data->magnitude;
    int r   = rand();
    int dx  = (r & 1)        ? mag : -mag;
    int dy  = ((r >> 1) & 1) ? mag : -mag;
    obj.pos.x = data->baselineX + (float)dx;
    obj.pos.y = data->baselineY + (float)dy;
    data->framesLeft--;
  }
}
