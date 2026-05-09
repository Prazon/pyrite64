/**
* Editor mirror of the runtime widgetSize() dispatch. Walks an Object's
* component list, asks the first sized component for its (w,h), returns
* (0,0) if none provide one. Sourced from CompInfo::funcWidgetSize so
* component schemas register their measurement next to their other hooks.
*/
#include "widgetSize.h"
#include "components.h"

namespace Project::Component
{
  WidgetSize widgetSize(Object &obj)
  {
    for (auto &entry : obj.components) {
      if (entry.id < 0 || (size_t)entry.id >= TABLE.size()) continue;
      const auto &info = TABLE[entry.id];
      if (!info.funcWidgetSize) continue;
      WidgetSize sz{};
      info.funcWidgetSize(obj, entry, &sz.w, &sz.h);
      if (sz.w > 0 || sz.h > 0) return sz;
    }
    return {};
  }
}
