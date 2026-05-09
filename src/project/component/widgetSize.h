/**
* Editor-side mirror of n64/engine/include/scene/widgetSize.h. Used by
* layout-container draw2D to position children at their runtime-equivalent
* positions in the canvas preview, and by other widget code that needs an
* intrinsic-size dispatch.
*/
#pragma once
#include "../scene/object.h"

namespace Project::Component
{
  struct WidgetSize { int w{0}; int h{0}; };

  // Returns the first 2D-eligible component's intrinsic size on `obj`, or
  // {0,0} if none. Order matches the runtime widgetSize() in the engine.
  WidgetSize widgetSize(Object &obj);
}
