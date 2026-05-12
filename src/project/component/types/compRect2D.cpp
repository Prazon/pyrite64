/**
* Editor-side Rect2D component.
* Pairs with engine: n64/engine/src/scene/components/rect2D.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

namespace Project::Component::Rect2D
{
  struct Data
  {
    PROP_S32(width);
    PROP_S32(height);
    PROP_S32(r);
    PROP_S32(g);
    PROP_S32(b);
    PROP_S32(a);
    PROP_S32(outlineThickness);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->width.value = 32;
    data->height.value = 32;
    data->r.value = 255;
    data->g.value = 255;
    data->b.value = 255;
    data->a.value = 255;
    data->outlineThickness.value = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder builder{};
    builder.set(data.width);
    builder.set(data.height);
    builder.set(data.r);
    builder.set(data.g);
    builder.set(data.b);
    builder.set(data.a);
    builder.set(data.outlineThickness);
    return builder.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->width, 32);
    Utils::JSON::readProp(doc, data->height, 32);
    Utils::JSON::readProp(doc, data->r, 255);
    Utils::JSON::readProp(doc, data->g, 255);
    Utils::JSON::readProp(doc, data->b, 255);
    Utils::JSON::readProp(doc, data->a, 255);
    Utils::JSON::readProp(doc, data->outlineThickness, 0);
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

    // Layout MUST match engine InitData in rect2D.cpp (sizeof == 10).
    ctx.fileObj.write<uint16_t>((uint16_t)data.width.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.height.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(clamp8(data.r.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.g.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.b.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.a.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.outlineThickness.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    int pxW = data.width.value  > 0 ? data.width.value  : 1;
    int pxH = data.height.value > 0 ? data.height.value : 1;
    float w = (float)pxW * zoom;
    float h = (float)pxH * zoom;
    ImVec2 br{originScreen.x + w, originScreen.y + h};
    ImU32 col = IM_COL32(clamp8(data.r.value), clamp8(data.g.value),
                         clamp8(data.b.value), clamp8(data.a.value));

    int t = data.outlineThickness.value;
    if (t == 0 || t * 2 >= pxW || t * 2 >= pxH) {
      dl->AddRectFilled(originScreen, br, col);
    } else {
      float tz = (float)t * zoom;
      // Four strips to mirror the engine outline path.
      dl->AddRectFilled(originScreen, {br.x, originScreen.y + tz}, col);
      dl->AddRectFilled({originScreen.x, br.y - tz}, br, col);
      dl->AddRectFilled({originScreen.x, originScreen.y + tz},
                        {originScreen.x + tz, br.y - tz}, col);
      dl->AddRectFilled({br.x - tz, originScreen.y + tz},
                        {br.x, br.y - tz}, col);
    }

    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
    (void)obj;
  }

  void widgetSize(Object &, Entry &entry, int *outW, int *outH)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (outW) *outW = data.width.value;
    if (outH) *outH = data.height.value;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Width", data.width);
      ImTable::addObjProp("Height", data.height);
      ImTable::addObjProp("R", data.r);
      ImTable::addObjProp("G", data.g);
      ImTable::addObjProp("B", data.b);
      ImTable::addObjProp("A", data.a);
      ImTable::addObjProp("Outline px (0 = filled)", data.outlineThickness);
      ImTable::end();
    }
  }
}
