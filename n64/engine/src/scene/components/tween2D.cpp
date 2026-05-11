/**
* Tween2D implementation.
* Easings:
*   Linear     -> t
*   Smoothstep -> t * t * (3 - 2 * t)
*   EaseIn     -> t * t
*   EaseOut    -> 1 - (1 - t) * (1 - t)
*/
#include "scene/components/tween2D.h"

namespace
{
  struct __attribute__((packed)) InitData
  {
    // Components on objects authored in the editor start inactive — the
    // tween is only configured at runtime via Tween2D::start(). The init
    // payload is just a placeholder default-easing for clarity.
    uint8_t defaultEasing;
    uint8_t _pad0;
    uint8_t _pad1;
    uint8_t _pad2;
  };
  static_assert(sizeof(InitData) == 4);

  inline float ease(uint8_t kind, float t)
  {
    switch (kind) {
      case 0: return t;
      case 1: return t * t * (3.0f - 2.0f * t);
      case 2: return t * t;
      case 3: { float u = 1.0f - t; return 1.0f - u * u; }
      default: return t;
    }
  }
}

namespace P64::Comp
{
  void Tween2D::initDelete(Object &obj, Tween2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;
    data->easing = src->defaultEasing;
    data->active = 0;
    data->age    = 0.0f;
    data->duration = 0.0f;
    (void)obj;
  }

  void Tween2D::start(
    Tween2D* data,
    float fromX, float fromY,
    float toX,   float toY,
    float durationSeconds,
    Easing easing
  ) {
    if (!data) return;
    if (durationSeconds <= 0.0f) {
      data->fromX = data->toX = toX;
      data->fromY = data->toY = toY;
      data->duration = 0.0f;
      data->age = 0.0f;
      data->active = 1;   // single-frame snap so update() places + clears
      data->easing = (uint8_t)easing;
      return;
    }
    data->fromX    = fromX;
    data->fromY    = fromY;
    data->toX      = toX;
    data->toY      = toY;
    data->duration = durationSeconds;
    data->age      = 0.0f;
    data->easing   = (uint8_t)easing;
    data->active   = 1;
  }

  void Tween2D::finish(Object &obj, Tween2D* data)
  {
    if (!data || !data->active) return;
    obj.pos.x = data->toX;
    obj.pos.y = data->toY;
    data->active = 0;
  }

  void Tween2D::update(Object &obj, Tween2D* data, float deltaTime)
  {
    if (!data->active) return;
    if (data->duration <= 0.0f) {
      obj.pos.x = data->toX;
      obj.pos.y = data->toY;
      data->active = 0;
      return;
    }
    data->age += deltaTime;
    float t = data->age / data->duration;
    if (t >= 1.0f) {
      obj.pos.x = data->toX;
      obj.pos.y = data->toY;
      data->active = 0;
      return;
    }
    if (t < 0.0f) t = 0.0f;
    float e = ease(data->easing, t);
    obj.pos.x = data->fromX + (data->toX - data->fromX) * e;
    obj.pos.y = data->fromY + (data->toY - data->fromY) * e;
  }
}
