/**
* Button2D implementation. Registers itself in the focus stack each frame;
* the focused button polls D-pad navigation + A-press and fires `eventType`
* on this Object via the existing event bus.
*/
#include "scene/components/button2D.h"
#include "scene/scene.h"
#include "scene/widgetFocus.h"
#include "renderer/drawLayer.h"
#include "assets/assetManager.h"

namespace
{
  // Layout matches compButton2D.cpp::build. argSize must be a multiple of
  // 4 (sceneLoader.cpp computes argSize as ptrIn[1] * 4) so the trailing
  // pad lands the struct at 20 bytes.
  struct __attribute__((packed)) InitData
  {
    uint16_t spriteNormalIdx;
    uint16_t spriteFocusIdx;
    uint16_t spritePressIdx;
    uint16_t width;
    uint16_t height;
    uint16_t eventType;
    uint8_t  initialFocus;
    uint8_t  alphaThreshold;
    uint8_t  tintR;
    uint8_t  tintG;
    uint8_t  tintB;
    uint8_t  tintA;
    uint16_t _pad;
  };
  static_assert(sizeof(InitData) == 20);

  inline sprite_t* loadSprite(uint16_t idx)
  {
    if (idx == 0xFFFF) return nullptr;
    return (sprite_t*)P64::AssetManager::getByIndex(idx);
  }
}

namespace P64::Comp
{
  void Button2D::initDelete(Object &obj, Button2D* data, void* initData_)
  {
    if (initData_ == nullptr) return;
    auto *src = (InitData*)initData_;

    data->width  = src->width  ? src->width  : 32;
    data->height = src->height ? src->height : 16;
    data->eventType = src->eventType;
    data->initialFocus = src->initialFocus;
    data->alphaThreshold = src->alphaThreshold;
    data->tintR = src->tintR;
    data->tintG = src->tintG;
    data->tintB = src->tintB;
    data->tintA = src->tintA;
    data->isPressed = 0;

    data->spriteNormal = loadSprite(src->spriteNormalIdx);
    data->spriteFocus  = loadSprite(src->spriteFocusIdx);
    data->spritePress  = loadSprite(src->spritePressIdx);

    if (data->initialFocus) {
      WidgetFocus::setFocus(&obj);
    }
  }

  void Button2D::update(Object &obj, Button2D* data, [[maybe_unused]] float deltaTime)
  {
    WidgetFocus::registerFocusable(obj);
    data->isPressed = 0;

    if (WidgetFocus::getFocused() != &obj) return;

    auto pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.d_left || pressed.d_up) {
      WidgetFocus::focusPrev();
    } else if (pressed.d_right || pressed.d_down) {
      WidgetFocus::focusNext();
    } else if (pressed.a) {
      data->isPressed = 1;
      Scene::getCurrent().sendEvent(obj.id, obj.id, data->eventType, 0);
    }
  }

  void Button2D::draw(Object &obj, Button2D* data, [[maybe_unused]] float deltaTime)
  {
    int x = (int)obj.pos.x;
    int y = (int)obj.pos.y;
    int w = (int)data->width;
    int h = (int)data->height;

    sprite_t *spr = data->spriteNormal;
    if (data->isPressed && data->spritePress) spr = data->spritePress;
    else if (WidgetFocus::getFocused() == &obj && data->spriteFocus) spr = data->spriteFocus;

    if (spr) {
      rdpq_blitparms_t parms{};
      parms.width   = spr->width;
      parms.height  = spr->height;
      parms.scale_x = (spr->width  > 0) ? ((float)w / (float)spr->width)  : 1.0f;
      parms.scale_y = (spr->height > 0) ? ((float)h / (float)spr->height) : 1.0f;

      rdpq_set_mode_standard();
      rdpq_mode_filter(FILTER_BILINEAR);
      if (data->alphaThreshold > 0) rdpq_mode_alphacompare(data->alphaThreshold);
      rdpq_set_prim_color({data->tintR, data->tintG, data->tintB, data->tintA});
      rdpq_sprite_blit(spr, x, y, &parms);
    } else {
      // Untextured fallback: solid rect with a focus highlight so the
      // button is still selectable when sprites haven't been assigned.
      color_t c = {data->tintR, data->tintG, data->tintB, data->tintA};
      if (WidgetFocus::getFocused() == &obj) c = {255, 220, 80, 255};
      if (data->isPressed) c = {255, 140, 40, 255};
      rdpq_set_mode_fill(c);
      rdpq_fill_rectangle(x, y, x + w, y + h);
    }
  }
}
