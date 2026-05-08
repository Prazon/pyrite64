/**
* Editor-side Label2D component.
* Pairs with engine: n64/engine/src/scene/components/label2D.cpp
*
* Variable-length text payload — the build emits a fixed 8-byte header
* followed by len bytes of UTF-8. The runtime malloc-copies the payload
* into a heap buffer.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../editor/undoRedo.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../assetManager.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Project::Component::Label2D
{
  struct Data
  {
    PROP_STRING(text);
    PROP_S32(fontSlot);
    PROP_S32(styleId);
    PROP_S32(colorR);
    PROP_S32(colorG);
    PROP_S32(colorB);
    PROP_S32(colorA);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->text.value = "Label";
    data->fontSlot.value = 0;
    data->styleId.value = 0;
    data->colorR.value = 255;
    data->colorG.value = 255;
    data->colorB.value = 255;
    data->colorA.value = 255;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.text);
    b.set(data.fontSlot);
    b.set(data.styleId);
    b.set(data.colorR);
    b.set(data.colorG);
    b.set(data.colorB);
    b.set(data.colorA);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->text, std::string{"Label"});
    Utils::JSON::readProp(doc, data->fontSlot, 0);
    Utils::JSON::readProp(doc, data->styleId, 0);
    Utils::JSON::readProp(doc, data->colorR, 255);
    Utils::JSON::readProp(doc, data->colorG, 255);
    Utils::JSON::readProp(doc, data->colorB, 255);
    Utils::JSON::readProp(doc, data->colorA, 255);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    const std::string &text = data.text.resolve(obj.propOverrides);
    uint16_t len = (uint16_t)std::min<size_t>(text.size(), 0xFFFE);

    auto clamp8 = [](int v) {
      if (v < 0) return (uint8_t)0;
      if (v > 255) return (uint8_t)255;
      return (uint8_t)v;
    };

    // Header layout MUST match engine InitDataHeader in label2D.cpp
    ctx.fileObj.write<uint16_t>(len);
    ctx.fileObj.write<uint8_t>((uint8_t)data.fontSlot.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>((uint8_t)data.styleId.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(clamp8(data.colorR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.colorG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.colorB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.colorA.resolve(obj.propOverrides)));

    for (uint16_t i = 0; i < len; ++i) ctx.fileObj.write<uint8_t>((uint8_t)text[i]);
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    ImU32 col = IM_COL32(
      clamp8(data.colorR.value), clamp8(data.colorG.value),
      clamp8(data.colorB.value), clamp8(data.colorA.value));

    // The N64-side font isn't loaded into ImGui, so this is a stand-in
    // using ImGui's default font scaled to track the canvas zoom level.
    // It tells the user *where* and *what color*, not exact glyph metrics.
    float fontSize = ImGui::GetFontSize() * (zoom * 0.5f < 0.5f ? 0.5f : zoom * 0.5f);
    const char *txt = data.text.value.c_str();
    dl->AddText(ImGui::GetFont(), fontSize, originScreen, col, txt);

    ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = ImVec2(originScreen.x + sz.x, originScreen.y + sz.y);
    (void)obj;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      ImTable::add("Text");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##text", &data.text.resolve(obj))) {
        Editor::UndoRedo::getHistory().markChanged("Edit Label Text");
      }

      ImTable::addObjProp("Font Slot", data.fontSlot);
      ImTable::addObjProp("Style", data.styleId);
      ImTable::addObjProp("Color R", data.colorR);
      ImTable::addObjProp("Color G", data.colorG);
      ImTable::addObjProp("Color B", data.colorB);
      ImTable::addObjProp("Color A", data.colorA);

      ImTable::end();
    }
  }
}
