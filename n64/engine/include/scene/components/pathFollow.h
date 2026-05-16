/**
* PathFollow component.
* Moves the object it is attached to along a Path component's spline by
* advancing an arc-length cursor every frame. Designed for on-rails
* cameras/ships (Star Fox-style), moving platforms, dollies and any
* "ride this curve" object.
*
* The follower is always the object this component lives on (its pos, and
* optionally rot, are written). The Path it follows is resolved in order:
*   1. a Path component on this same object,
*   2. a Path component on the parent object,
*   3. the explicit target object (refObjId) picked in the editor.
* Branch/alternate-route selection is handled entirely by the Path
* component via PathRT; PathFollow just samples the active group.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::Comp
{
  struct PathFollow
  {
    static constexpr uint32_t ID = 31;

    static constexpr uint8_t MODE_ONCE     = 0; // travel to end, then stop
    static constexpr uint8_t MODE_LOOP     = 1; // wrap back to 0
    static constexpr uint8_t MODE_PINGPONG = 2; // reverse at each end

    static constexpr uint8_t FLAG_ORIENT   = 1 << 0; // write obj.rot from frame
    static constexpr uint8_t FLAG_AUTOPLAY = 1 << 1; // start moving at init

    // Runtime state. Public so scripts / event-graph nodes can pause, seek,
    // change speed or retarget the follower at runtime.
    float    distance{0.0f};   // current arc length along the active spline
    float    speed{0.0f};      // units / second; sign flips for ping-pong
    uint16_t refObjId{0};      // explicit Path owner; 0 = auto (self/parent)
    uint8_t  mode{MODE_ONCE};
    uint8_t  flags{0};
    int8_t   dir{1};           // ping-pong travel direction (+1 / -1)
    uint8_t  playing{0};

    static uint32_t getAllocSize([[maybe_unused]] void* initData)
    {
      return sizeof(PathFollow);
    }

    static void initDelete(Object& obj, PathFollow* data, void* initData);
    static void update(Object& obj, PathFollow* data, float deltaTime);
  };
}
