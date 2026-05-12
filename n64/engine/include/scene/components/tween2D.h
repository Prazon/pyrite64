/**
* Tween2D component.
* Drives obj.pos.x/y from a captured "from" point to a configured "to"
* point over `duration` seconds with one of three easings. Gameplay code
* calls Tween2D::start(data, fromX, fromY, toX, toY, durationSeconds,
* easing); update() advances per frame, writes the interpolated pos
* directly into obj.pos. When age >= duration the position snaps to
* (toX, toY) and `active` clears.
*
* Smoothstep matches Pixic's `t*t*(3-2*t)` exactly so falling-block and
* rotation-block animations port without re-tuning.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct Tween2D
  {
    static constexpr uint32_t ID = 28;

    enum class Easing : uint8_t
    {
      Linear     = 0,
      Smoothstep = 1,
      EaseIn     = 2,   // quadratic ease-in
      EaseOut    = 3,   // quadratic ease-out
    };

    float    fromX{0};
    float    fromY{0};
    float    toX{0};
    float    toY{0};
    float    age{0};
    float    duration{0};
    uint8_t  easing{0};
    uint8_t  active{0};
    uint8_t  _pad0{0};
    uint8_t  _pad1{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) {
      return sizeof(Tween2D);
    }

    static void initDelete(Object& obj, Tween2D* data, void* initData);
    static void update(Object& obj, Tween2D* data, float deltaTime);
    static void draw([[maybe_unused]] Object& obj, [[maybe_unused]] Tween2D* data,
                     [[maybe_unused]] float deltaTime) {}

    static void start(
      Tween2D* data,
      float fromX, float fromY,
      float toX,   float toY,
      float durationSeconds,
      Easing easing
    );

    // Snap to end and mark inactive without animating.
    static void finish(Object &obj, Tween2D* data);

    static bool isActive(const Tween2D* data) { return data && data->active != 0; }
  };
}
