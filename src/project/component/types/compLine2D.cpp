/**
* Editor-side Line2D component.
* Pairs with engine: n64/engine/src/scene/components/line2D.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

#include <cmath>

namespace Project::Component::Line2D
{
  struct Data
  {
    PROP_S32(dx);
    PROP_S32(dy);
    PROP_S32(r);
    PROP_S32(g);
    PROP_S32(b);
    PROP_S32(a);
    PROP_S32(thickness);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->dx.value = 16;
    data->dy.value = 0;
    data->r.value = 255;
    data->g.value = 255;
    data->b.value = 255;
    data->a.value = 255;
    data->thickness.value = 1;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder builder{};
    builder.set(data.dx);
    builder.set(data.dy);
    builder.set(data.r);
    builder.set(data.g);
    builder.set(data.b);
    builder.set(data.a);
    builder.set(data.thickness);
    return builder.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->dx, 16);
    Utils::JSON::readProp(doc, data->dy, 0);
    Utils::JSON::readProp(doc, data->r, 255);
    Utils::JSON::readProp(doc, data->g, 255);
    Utils::JSON::readProp(doc, data->b, 255);
    Utils::JSON::readProp(doc, data->a, 255);
    Utils::JSON::readProp(doc, data->thickness, 1);
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

    // Layout MUST match engine InitData in line2D.cpp (sizeof == 12).
    ctx.fileObj.write<int16_t>((int16_t)data.dx.resolve(obj.propOverrides));
    ctx.fileObj.write<int16_t>((int16_t)data.dy.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(clamp8(data.r.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.g.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.b.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.a.resolve(obj.propOverrides)));
    int th = data.thickness.resolve(obj.propOverrides);
    if (th < 1) th = 1;
    if (th > 255) th = 255;
    ctx.fileObj.write<uint8_t>((uint8_t)th);
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

    // dx/dy are obj.pos-relative. originScreen already encodes obj.pos in
    // window coords, so the line's end is just originScreen + delta*zoom.
    ImVec2 a = originScreen;
    ImVec2 b{originScreen.x + (float)data.dx.value * zoom,
             originScreen.y + (float)data.dy.value * zoom};

    ImU32 col = IM_COL32(clamp8(data.r.value), clamp8(data.g.value),
                         clamp8(data.b.value), clamp8(data.a.value));
    float th = (float)(data.thickness.value < 1 ? 1 : data.thickness.value) * zoom;
    if (th < 1.0f) th = 1.0f;
    dl->AddLine(a, b, col, th);

    if (outMin) *outMin = ImVec2(std::min(a.x, b.x), std::min(a.y, b.y));
    if (outMax) *outMax = ImVec2(std::max(a.x, b.x), std::max(a.y, b.y));
    (void)obj;
  }

  void widgetSize(Object &, Entry &entry, int *outW, int *outH)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int dx = data.dx.value < 0 ? -data.dx.value : data.dx.value;
    int dy = data.dy.value < 0 ? -data.dy.value : data.dy.value;
    if (outW) *outW = dx;
    if (outH) *outH = dy;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("End X (relative)", data.dx);
      ImTable::addObjProp("End Y (relative)", data.dy);
      ImTable::addObjProp("R", data.r);
      ImTable::addObjProp("G", data.g);
      ImTable::addObjProp("B", data.b);
      ImTable::addObjProp("A", data.a);
      ImTable::addObjProp("Thickness", data.thickness);
      ImTable::end();
    }
  }
}
