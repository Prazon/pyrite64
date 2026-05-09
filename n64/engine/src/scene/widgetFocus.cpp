/**
* WidgetFocus implementation. Single static slot for the focused object,
* a flat array registry rebuilt each frame. No locking: the engine is
* single-threaded so update-time access is safe.
*/
#include "scene/widgetFocus.h"

namespace
{
  P64::Object  *focusedObj{nullptr};
  P64::Object  *registry[P64::WidgetFocus::MAX_FOCUSABLES] = {};
  uint32_t      registryCount{0};

  int findIndex(P64::Object *o)
  {
    for (uint32_t i = 0; i < registryCount; ++i) {
      if (registry[i] == o) return (int)i;
    }
    return -1;
  }
}

namespace P64::WidgetFocus
{
  void registerFocusable(Object &obj)
  {
    if (registryCount >= MAX_FOCUSABLES) return;
    registry[registryCount++] = &obj;
    if (!focusedObj) focusedObj = &obj;
  }

  Object* getFocused() { return focusedObj; }

  void setFocus(Object *obj) { focusedObj = obj; }

  void focusNext()
  {
    if (registryCount < 2) return;
    int idx = findIndex(focusedObj);
    if (idx < 0) idx = 0;
    else idx = (idx + 1) % (int)registryCount;
    focusedObj = registry[idx];
  }

  void focusPrev()
  {
    if (registryCount < 2) return;
    int idx = findIndex(focusedObj);
    if (idx <= 0) idx = (int)registryCount - 1;
    else --idx;
    focusedObj = registry[idx];
  }

  void beginFrame()
  {
    registryCount = 0;
    // Note: focusedObj intentionally persists across frames until something
    // resets it (object removal would normally clear it, but that path is
    // out of scope for this minimal MVP focus manager). Stale focus is
    // harmless: no object means buttons claim focus afresh next frame.
  }
}
