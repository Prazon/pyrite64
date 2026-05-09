/**
* HBoxLayout implementation. Layout in update() so positions are stable for
* the same-frame draw walk.
*/
#include "scene/components/hboxLayout.h"
#include "scene/scene.h"
#include "scene/widgetSize.h"

namespace
{
  // Layout matches compHBoxLayout.cpp::build (sizeof == 4).
  struct __attribute__((packed)) InitData
  {
    int16_t spacing;
    uint8_t alignY;
    uint8_t _pad;
  };
  static_assert(sizeof(InitData) == 4);
}

namespace P64::Comp
{
  void HBoxLayout::initDelete(Object &obj, HBoxLayout* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;
    data->spacing = src->spacing;
    data->alignY  = src->alignY;
    (void)obj;
  }

  void HBoxLayout::update(Object &obj, HBoxLayout* data, [[maybe_unused]] float deltaTime)
  {
    // Two-pass: first compute the column max-height for vertical alignment,
    // then place children.
    int maxH = 0;
    obj.iterChildren([&](Object* child) {
      if (!child || !child->isEnabled()) return;
      auto sz = widgetSize(*child);
      if (sz.h > maxH) maxH = sz.h;
    });

    int curX = (int)obj.pos.x;
    int baseY = (int)obj.pos.y;

    obj.iterChildren([&](Object* child) {
      if (!child || !child->isEnabled()) return;
      auto sz = widgetSize(*child);

      int yOff = 0;
      switch (data->alignY) {
        case 1: yOff = (maxH - sz.h) / 2; break;
        case 2: yOff = (maxH - sz.h);     break;
        default: yOff = 0;                break;
      }

      child->pos.x = (float)curX;
      child->pos.y = (float)(baseY + yOff);
      curX += sz.w + data->spacing;
    });
  }
}
