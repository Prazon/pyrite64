/**
* Editor-side Tween2D component.
* Pairs with engine: n64/engine/src/scene/components/tween2D.cpp
*
* The editor only persists a default-easing field. Per-tween from/to/
* duration are set at runtime by gameplay code via Tween2D::start().
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

namespace Project::Component::Tween2D
{
  struct Data
  {
    PROP_S32(defaultEasing);   // 0=Linear, 1=Smoothstep, 2=EaseIn, 3=EaseOut
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->defaultEasing.value = 1;  // Smoothstep matches Pixic's t*t*(3-2t).
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.defaultEasing);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->defaultEasing, 1);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int e = data.defaultEasing.resolve(obj.propOverrides);
    if (e < 0) e = 0;
    if (e > 3) e = 3;
    // Layout MUST match engine InitData in tween2D.cpp (sizeof == 4).
    ctx.fileObj.write<uint8_t>((uint8_t)e);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    // Behavior-only component; show a small icon marker so it's selectable.
    float r = 5.0f * (zoom < 1.0f ? 1.0f : zoom);
    dl->AddCircle(originScreen, r, IM_COL32(150, 220, 255, 220), 0, 1.5f);
    dl->AddText({originScreen.x + r + 2.0f, originScreen.y - 6.0f},
                IM_COL32(150, 220, 255, 220), "tween");
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
      constexpr const char *EASINGS[] = {"Linear", "Smoothstep", "EaseIn", "EaseOut"};
      ImTable::addComboBox("Default easing", data.defaultEasing.value, EASINGS, 4);
      ImTable::end();
    }
  }
}
