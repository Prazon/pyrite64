/**
* Tiny focus stack for keyboard-style widget input on N64. No mouse, so
* "the focused widget receives A-press" is the entire model. Buttons
* (and any future focusable widgets) register themselves in update() and
* the focused one polls input.
*
* This is intentionally minimal: a single global focused-object slot plus
* a per-frame registry rebuilt from update(). Sequential D-pad navigation
* walks the registry by registration order. Game scripts can override
* initial focus with setFocus() at any point.
*/
#pragma once
#include <libdragon.h>
#include "scene/object.h"

namespace P64::WidgetFocus
{
  constexpr uint32_t MAX_FOCUSABLES = 32;

  // Called by focusable component update()s before they read input. Adds
  // the object to this frame's focusable list. The first registrant of a
  // frame after a focus loss claims focus.
  void registerFocusable(Object &obj);

  // Returns the currently focused Object, or nullptr if none.
  Object* getFocused();

  // Override the focused object. Pass nullptr to clear.
  void setFocus(Object *obj);

  // Move focus to the next / previous focusable in the most recent
  // registry order. No-op when fewer than two focusables exist this frame.
  void focusNext();
  void focusPrev();

  // Resets the per-frame registry. Called by Scene::update at the start
  // of each tick so registrations don't carry over across frames.
  void beginFrame();
}
