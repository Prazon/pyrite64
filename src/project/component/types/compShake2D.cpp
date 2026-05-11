/**
* Editor-side Shake2D component.
* Pairs with engine: n64/engine/src/scene/components/shake2D.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

namespace Project::Component::Shake2D
{
  struct Data
  {
    PROP_S32(defaultMagnitude);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->defaultMagnitude.value = 1;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.defaultMagnitude);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->defaultMagnitude, 1);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int m = data.defaultMagnitude.resolve(obj.propOverrides);
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    // Layout MUST match engine InitData in shake2D.cpp (sizeof == 4).
    ctx.fileObj.write<uint8_t>((uint8_t)m);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    float r = 5.0f * (zoom < 1.0f ? 1.0f : zoom);
    dl->AddCircle(originScreen, r, IM_COL32(255, 180, 100, 220), 0, 1.5f);
    dl->AddText({originScreen.x + r + 2.0f, originScreen.y - 6.0f},
                IM_COL32(255, 180, 100, 220), "shake");
    if (outMin) *outMin = ImVec2(originScreen.x - r, originScreen.y - r);
    if (outMax) *outMax = ImVec2(originScreen.x + r, originScreen.y + r);
    (void)entry;
    (void)obj;
  }

  void widgetSize(Object &, Entry &, int *outW, int *outH)
  {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Default magnitude (px)", data.defaultMagnitude);
      ImTable::end();
    }
  }
}
