/**
* Editor-side VBoxLayout component.
* Pairs with engine: n64/engine/src/scene/components/vboxLayout.cpp.
*/
#include "../components.h"
#include "../widgetSize.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"

#include "imgui.h"

namespace Project::Component::VBoxLayout
{
  struct Data
  {
    PROP_S32(spacing);
    PROP_S32(alignX);    // 0=left, 1=center, 2=right
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->spacing.value = 2;
    data->alignX.value  = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spacing);
    b.set(data.alignX);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spacing, 2);
    Utils::JSON::readProp(doc, data->alignX, 0);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int spacing = data.spacing.resolve(obj.propOverrides);
    if (spacing < INT16_MIN) spacing = INT16_MIN;
    if (spacing > INT16_MAX) spacing = INT16_MAX;
    int align = data.alignX.resolve(obj.propOverrides);
    if (align < 0) align = 0;
    if (align > 2) align = 2;

    // Layout MUST match engine InitData in vboxLayout.cpp (sizeof == 4)
    ctx.fileObj.write<int16_t>((int16_t)spacing);
    ctx.fileObj.write<uint8_t>((uint8_t)align);
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    int maxW = 0, totalH = 0;
    for (auto &child : obj.children) {
      auto sz = ::Project::Component::widgetSize(*child);
      if (sz.w > 0 || sz.h > 0) {
        if (totalH > 0) totalH += 2;
        totalH += sz.h;
        if (sz.w > maxW) maxW = sz.w;
      }
    }
    if (maxW <= 0) { maxW = 16; totalH = 16; }

    ImVec2 br{originScreen.x + (float)maxW * zoom,
              originScreen.y + (float)totalH * zoom};
    dl->AddRect(originScreen, br, IM_COL32(120, 255, 160, 160), 0.0f, 0, 1.0f);
    dl->AddText({originScreen.x + 2, originScreen.y - 14},
                IM_COL32(120, 255, 160, 220), "VBox");
    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Spacing", data.spacing);
      ImTable::addObjProp("Align X (0=left,1=center,2=right)", data.alignX);
      ImTable::end();
    }
  }
}
