/**
* Editor-side Button2D component.
* Pairs with engine: n64/engine/src/scene/components/button2D.cpp.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../assetManager.h"

#include "imgui.h"

#include <cctype>
#include <string>

namespace Project::Component::Button2D
{
  struct Data
  {
    PROP_U64(spriteNormalUUID);
    PROP_U64(spriteFocusUUID);
    PROP_U64(spritePressUUID);
    PROP_S32(width);
    PROP_S32(height);
    PROP_S32(eventType);
    PROP_BOOL(initialFocus);
    PROP_S32(alphaThreshold);
    PROP_S32(tintR);
    PROP_S32(tintG);
    PROP_S32(tintB);
    PROP_S32(tintA);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->width.value  = 64;
    data->height.value = 16;
    data->eventType.value = 0;
    data->initialFocus.value = false;
    data->alphaThreshold.value = 1;
    data->tintR.value = 255;
    data->tintG.value = 255;
    data->tintB.value = 255;
    data->tintA.value = 255;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spriteNormalUUID);
    b.set(data.spriteFocusUUID);
    b.set(data.spritePressUUID);
    b.set(data.width);
    b.set(data.height);
    b.set(data.eventType);
    b.set(data.initialFocus);
    b.set(data.alphaThreshold);
    b.set(data.tintR);
    b.set(data.tintG);
    b.set(data.tintB);
    b.set(data.tintA);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spriteNormalUUID);
    Utils::JSON::readProp(doc, data->spriteFocusUUID);
    Utils::JSON::readProp(doc, data->spritePressUUID);
    Utils::JSON::readProp(doc, data->width, 64);
    Utils::JSON::readProp(doc, data->height, 16);
    Utils::JSON::readProp(doc, data->eventType, 0);
    Utils::JSON::readProp(doc, data->initialFocus);
    Utils::JSON::readProp(doc, data->alphaThreshold, 1);
    Utils::JSON::readProp(doc, data->tintR, 255);
    Utils::JSON::readProp(doc, data->tintG, 255);
    Utils::JSON::readProp(doc, data->tintB, 255);
    Utils::JSON::readProp(doc, data->tintA, 255);
    return data;
  }

  static uint16_t resolveSpriteIdx(uint64_t uuid, Build::SceneCtx &ctx)
  {
    if (uuid == 0) return 0xFFFF;
    auto res = ctx.assetUUIDToIdx.find(uuid);
    if (res != ctx.assetUUIDToIdx.end()) return res->second;
    Utils::Logger::log(
      "Button2D: sprite UUID not found: " + std::to_string(uuid),
      Utils::Logger::LEVEL_WARN);
    return 0xFFFF;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto clamp8 = [](int v) {
      if (v < 0) return (uint8_t)0;
      if (v > 255) return (uint8_t)255;
      return (uint8_t)v;
    };
    auto clamp16 = [](int v) {
      if (v < 0) return (uint16_t)0;
      if (v > 65535) return (uint16_t)65535;
      return (uint16_t)v;
    };

    // Layout MUST match engine InitData in button2D.cpp (sizeof == 20)
    ctx.fileObj.write<uint16_t>(resolveSpriteIdx(data.spriteNormalUUID.value, ctx));
    ctx.fileObj.write<uint16_t>(resolveSpriteIdx(data.spriteFocusUUID.value, ctx));
    ctx.fileObj.write<uint16_t>(resolveSpriteIdx(data.spritePressUUID.value, ctx));
    ctx.fileObj.write<uint16_t>((uint16_t)data.width.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.height.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>(clamp16(data.eventType.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(data.initialFocus.resolve(obj.propOverrides) ? 1 : 0);
    ctx.fileObj.write<uint8_t>(clamp8(data.alphaThreshold.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintA.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint16_t>(0); // pad to 20 bytes
  }

  void widgetSize(Object &, Entry &entry, int *outW, int *outH)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (outW) *outW = data.width.value;
    if (outH) *outH = data.height.value;
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    int  pxW = data.width.value  > 0 ? data.width.value  : 1;
    int  pxH = data.height.value > 0 ? data.height.value : 1;
    float w = (float)pxW * zoom;
    float h = (float)pxH * zoom;
    ImVec2 br{originScreen.x + w, originScreen.y + h};

    auto *e = (data.spriteNormalUUID.value != 0)
                ? ctx.project->getAssets().getEntryByUUID(data.spriteNormalUUID.value)
                : nullptr;
    SDL_GPUTexture *gpu = (e && e->texture) ? e->texture->getGPUTex() : nullptr;

    if (gpu) {
      ImU32 tint = IM_COL32(clamp8(data.tintR.value), clamp8(data.tintG.value),
                            clamp8(data.tintB.value), clamp8(data.tintA.value));
      dl->AddImage(ImTextureID(gpu), originScreen, br, ImVec2(0, 0), ImVec2(1, 1), tint);
    } else {
      dl->AddRectFilled(originScreen, br, IM_COL32(80, 80, 100, 220));
    }
    dl->AddRect(originScreen, br,
      data.initialFocus.value ? IM_COL32(255, 220, 80, 255)
                              : IM_COL32(160, 160, 200, 220),
      0.0f, 0, data.initialFocus.value ? 2.0f : 1.0f);

    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
    (void)obj;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Width",  data.width);
      ImTable::addObjProp("Height", data.height);
      ImTable::addObjProp("Event Type", data.eventType);
      ImTable::addObjProp("Initial Focus", data.initialFocus);
      ImTable::addObjProp("Alpha Threshold", data.alphaThreshold);
      ImTable::addObjProp("Tint R", data.tintR);
      ImTable::addObjProp("Tint G", data.tintG);
      ImTable::addObjProp("Tint B", data.tintB);
      ImTable::addObjProp("Tint A", data.tintA);

      // Sprite slots are stored in serialize/deserialize but a thumbnail
      // picker is deferred (the same UI as Sprite2D's drawImagePicker would
      // duplicate that helper's body four times). For now buttons either
      // ship with all three sprite UUIDs zeroed (untextured fallback in the
      // engine) or are populated by direct .p64widget edits / CLI.
      ImTable::add("Sprite Normal");
      ImGui::TextDisabled("uuid: %llu", (unsigned long long)data.spriteNormalUUID.value);
      ImTable::add("Sprite Focus");
      ImGui::TextDisabled("uuid: %llu", (unsigned long long)data.spriteFocusUUID.value);
      ImTable::add("Sprite Press");
      ImGui::TextDisabled("uuid: %llu", (unsigned long long)data.spritePressUUID.value);

      ImTable::end();
    }
  }
}
