/**
* Editor-side ProgressBar2D component.
* Pairs with engine: n64/engine/src/scene/components/progressBar2D.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

namespace Project::Component::ProgressBar2D
{
  struct Data
  {
    PROP_S32(width);
    PROP_S32(height);
    PROP_FLOAT(value);
    PROP_S32(bgR);
    PROP_S32(bgG);
    PROP_S32(bgB);
    PROP_S32(bgA);
    PROP_S32(fgR);
    PROP_S32(fgG);
    PROP_S32(fgB);
    PROP_S32(fgA);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->width.value = 64;
    data->height.value = 8;
    data->value.value = 1.0f;
    data->bgR.value = 32;
    data->bgG.value = 32;
    data->bgB.value = 32;
    data->bgA.value = 255;
    data->fgR.value = 220;
    data->fgG.value = 50;
    data->fgB.value = 50;
    data->fgA.value = 255;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.width);
    b.set(data.height);
    b.set(data.value);
    b.set(data.bgR);
    b.set(data.bgG);
    b.set(data.bgB);
    b.set(data.bgA);
    b.set(data.fgR);
    b.set(data.fgG);
    b.set(data.fgB);
    b.set(data.fgA);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->width, 64);
    Utils::JSON::readProp(doc, data->height, 8);
    Utils::JSON::readProp(doc, data->value, 1.0f);
    Utils::JSON::readProp(doc, data->bgR, 32);
    Utils::JSON::readProp(doc, data->bgG, 32);
    Utils::JSON::readProp(doc, data->bgB, 32);
    Utils::JSON::readProp(doc, data->bgA, 255);
    Utils::JSON::readProp(doc, data->fgR, 220);
    Utils::JSON::readProp(doc, data->fgG, 50);
    Utils::JSON::readProp(doc, data->fgB, 50);
    Utils::JSON::readProp(doc, data->fgA, 255);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto clamp8 = [](int v) {
      if (v < 0) return (uint8_t)0;
      if (v > 255) return (uint8_t)255;
      return (uint8_t)v;
    };

    // Layout MUST match engine InitData in progressBar2D.cpp (sizeof == 20)
    ctx.fileObj.write<uint16_t>((uint16_t)data.width.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.height.resolve(obj.propOverrides));
    ctx.fileObj.write<float>(data.value.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(clamp8(data.bgR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.bgG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.bgB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.bgA.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.fgR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.fgG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.fgB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.fgA.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
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
    float v = data.value.value;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    ImVec2 br{originScreen.x + w, originScreen.y + h};
    ImU32 cBG = IM_COL32(clamp8(data.bgR.value), clamp8(data.bgG.value),
                         clamp8(data.bgB.value), clamp8(data.bgA.value));
    ImU32 cFG = IM_COL32(clamp8(data.fgR.value), clamp8(data.fgG.value),
                         clamp8(data.fgB.value), clamp8(data.fgA.value));
    dl->AddRectFilled(originScreen, br, cBG);
    if (v > 0.0f) {
      ImVec2 fbr{originScreen.x + w * v, br.y};
      dl->AddRectFilled(originScreen, fbr, cFG);
    }
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
      ImTable::addObjProp("Value (0..1)", data.value);
      ImTable::addObjProp("BG R", data.bgR);
      ImTable::addObjProp("BG G", data.bgG);
      ImTable::addObjProp("BG B", data.bgB);
      ImTable::addObjProp("BG A", data.bgA);
      ImTable::addObjProp("FG R", data.fgR);
      ImTable::addObjProp("FG G", data.fgG);
      ImTable::addObjProp("FG B", data.fgB);
      ImTable::addObjProp("FG A", data.fgA);
      ImTable::end();
    }
  }
}
