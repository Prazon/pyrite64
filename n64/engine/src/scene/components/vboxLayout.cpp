/**
* VBoxLayout implementation. Vertical sibling of HBoxLayout.
*/
#include "scene/components/vboxLayout.h"
#include "scene/scene.h"
#include "scene/widgetSize.h"

namespace
{
  // Layout matches compVBoxLayout.cpp::build (sizeof == 4).
  struct __attribute__((packed)) InitData
  {
    int16_t spacing;
    uint8_t alignX;
    uint8_t _pad;
  };
  static_assert(sizeof(InitData) == 4);
}

namespace P64::Comp
{
  void VBoxLayout::initDelete(Object &obj, VBoxLayout* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;
    data->spacing = src->spacing;
    data->alignX  = src->alignX;
    (void)obj;
  }

  void VBoxLayout::update(Object &obj, VBoxLayout* data, [[maybe_unused]] float deltaTime)
  {
    int maxW = 0;
    obj.iterChildren([&](Object* child) {
      if (!child || !child->isEnabled()) return;
      auto sz = widgetSize(*child);
      if (sz.w > maxW) maxW = sz.w;
    });

    int baseX = (int)obj.pos.x;
    int curY = (int)obj.pos.y;

    obj.iterChildren([&](Object* child) {
      if (!child || !child->isEnabled()) return;
      auto sz = widgetSize(*child);

      int xOff = 0;
      switch (data->alignX) {
        case 1: xOff = (maxW - sz.w) / 2; break;
        case 2: xOff = (maxW - sz.w);     break;
        default: xOff = 0;                break;
      }

      child->pos.x = (float)(baseX + xOff);
      child->pos.y = (float)curY;
      curY += sz.h + data->spacing;
    });
  }
}
