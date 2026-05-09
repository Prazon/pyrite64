/**
* Intrinsic-size dispatch for layout containers (HBox, VBox).
*
* Each layout container queries every enabled child's intrinsic (w,h) so it
* can position siblings without knowing the concrete component types. The
* dispatch is component-id based; new widget components add a case here.
*
* The editor mirrors this logic in src/project/component/widgetSize.cpp so
* the WYSIWYG canvas previews layout positions identically. Field names
* must stay aligned across both files.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"
#include "scene/components/sprite2D.h"
#include "scene/components/label2D.h"
#include "scene/components/progressBar2D.h"
#include "scene/components/panel2D.h"
#include "scene/components/ninePatch2D.h"

namespace P64
{
  struct WidgetSize
  {
    int w{0};
    int h{0};
  };

  inline WidgetSize widgetSize(Object &obj)
  {
    if (auto *p = obj.getComponent<Comp::Sprite2D>()) {
      int cw = p->cellW ? p->cellW : (p->sprite ? p->sprite->width : 0);
      int ch = p->cellH ? p->cellH : (p->sprite ? p->sprite->height : 0);
      int q  = p->pixelScaleQ ? p->pixelScaleQ : 16;
      return { (cw * q) / 16, (ch * q) / 16 };
    }
    if (auto *p = obj.getComponent<Comp::ProgressBar2D>()) {
      return { (int)p->width, (int)p->height };
    }
    if (auto *p = obj.getComponent<Comp::Panel2D>()) {
      return { (int)p->width, (int)p->height };
    }
    if (auto *p = obj.getComponent<Comp::NinePatch2D>()) {
      return { (int)p->width, (int)p->height };
    }
    if (auto *p = obj.getComponent<Comp::Label2D>()) {
      // Rough estimate: rdpq_text exposes no measurement API on this fork.
      // Width = 6px-per-char of strlen, height = 10px (typical font cap +
      // descender). Layout containers should treat label cells as flexible
      // anyway since text is variable-width.
      int len = 0;
      if (p->text) {
        for (const char *c = p->text; *c; ++c) ++len;
      }
      return { len * 6, 10 };
    }
    return { 0, 0 };
  }
}
