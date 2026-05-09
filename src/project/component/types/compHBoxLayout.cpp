/**
* Editor-side HBoxLayout component.
* Pairs with engine: n64/engine/src/scene/components/hboxLayout.cpp.
*
* draw2D paints a translucent dashed bracket around the layout's child run
* so the user can see where the container occupies canvas space; child
* positions themselves are written by the runtime update() so the canvas
* preview just measures.
*/
#include "../components.h"
#include "../widgetSize.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"

#include "imgui.h"

namespace Project::Component::HBoxLayout
{
  struct Data
  {
    PROP_S32(spacing);
    PROP_S32(alignY);    // 0=top, 1=center, 2=bottom
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->spacing.value = 2;
    data->alignY.value  = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spacing);
    b.set(data.alignY);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spacing, 2);
    Utils::JSON::readProp(doc, data->alignY, 0);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int spacing = data.spacing.resolve(obj.propOverrides);
    if (spacing < INT16_MIN) spacing = INT16_MIN;
    if (spacing > INT16_MAX) spacing = INT16_MAX;
    int align = data.alignY.resolve(obj.propOverrides);
    if (align < 0) align = 0;
    if (align > 2) align = 2;

    // Layout MUST match engine InitData in hboxLayout.cpp (sizeof == 4)
    ctx.fileObj.write<int16_t>((int16_t)spacing);
    ctx.fileObj.write<uint8_t>((uint8_t)align);
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    // Estimate the laid-out run width by walking the children's intrinsic
    // sizes the same way the runtime will. Visualizes a dashed bracket
    // around the column so the container is selectable in the canvas even
    // when it has no children yet.
    int totalW = 0, maxH = 0;
    for (auto &child : obj.children) {
      auto sz = ::Project::Component::widgetSize(*child);
      if (sz.w > 0 || sz.h > 0) {
        if (totalW > 0) totalW += 2; // approx default spacing
        totalW += sz.w;
        if (sz.h > maxH) maxH = sz.h;
      }
    }
    if (totalW <= 0) { totalW = 16; maxH = 8; }

    ImVec2 br{originScreen.x + (float)totalW * zoom,
              originScreen.y + (float)maxH * zoom};
    dl->AddRect(originScreen, br, IM_COL32(120, 200, 255, 160), 0.0f, 0, 1.0f);
    dl->AddText({originScreen.x + 2, originScreen.y - 14},
                IM_COL32(120, 200, 255, 220), "HBox");
    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Spacing", data.spacing);
      ImTable::addObjProp("Align Y (0=top,1=center,2=bottom)", data.alignY);
      ImTable::end();
    }
  }
}
