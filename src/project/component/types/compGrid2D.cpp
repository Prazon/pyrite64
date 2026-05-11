/**
* Editor-side Grid2D component.
* Pairs with engine: n64/engine/src/scene/components/grid2D.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../assetManager.h"
#include "../../../editor/undoRedo.h"

#include "imgui.h"

#include <cctype>
#include <string>

namespace Project::Component::Grid2D
{
  struct Data
  {
    PROP_U64(tilesetUUID);
    PROP_S32(width);
    PROP_S32(height);
    PROP_S32(cellW);
    PROP_S32(cellH);
    PROP_S32(shakeMagnitude);
    PROP_S32(alphaThreshold);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->width.value = 8;
    data->height.value = 8;
    data->cellW.value = 12;
    data->cellH.value = 12;
    data->shakeMagnitude.value = 1;
    data->alphaThreshold.value = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.tilesetUUID);
    b.set(data.width);
    b.set(data.height);
    b.set(data.cellW);
    b.set(data.cellH);
    b.set(data.shakeMagnitude);
    b.set(data.alphaThreshold);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->tilesetUUID);
    Utils::JSON::readProp(doc, data->width, 8);
    Utils::JSON::readProp(doc, data->height, 8);
    Utils::JSON::readProp(doc, data->cellW, 12);
    Utils::JSON::readProp(doc, data->cellH, 12);
    Utils::JSON::readProp(doc, data->shakeMagnitude, 1);
    Utils::JSON::readProp(doc, data->alphaThreshold, 0);
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

    uint16_t spriteIdx = 0xFFFF;
    if (data.tilesetUUID.value != 0) {
      auto res = ctx.assetUUIDToIdx.find(data.tilesetUUID.value);
      if (res != ctx.assetUUIDToIdx.end()) spriteIdx = res->second;
      else Utils::Logger::log(
        "Grid2D: tileset UUID not found: " + std::to_string(data.tilesetUUID.value),
        Utils::Logger::LEVEL_WARN
      );
    }

    int w  = data.width.resolve(obj.propOverrides);
    int h  = data.height.resolve(obj.propOverrides);
    int cw = data.cellW.resolve(obj.propOverrides);
    int ch = data.cellH.resolve(obj.propOverrides);
    if (w  < 1)  w  = 1;
    if (h  < 1)  h  = 1;
    if (cw < 1)  cw = 1;
    if (ch < 1)  ch = 1;
    if (w  > 64) w  = 64;
    if (h  > 64) h  = 64;

    // Layout MUST match engine InitData in grid2D.cpp (sizeof == 16).
    ctx.fileObj.write<uint16_t>(spriteIdx);
    ctx.fileObj.write<uint16_t>((uint16_t)w);
    ctx.fileObj.write<uint16_t>((uint16_t)h);
    ctx.fileObj.write<uint16_t>((uint16_t)cw);
    ctx.fileObj.write<uint16_t>((uint16_t)ch);
    ctx.fileObj.write<uint8_t>(clamp8(data.shakeMagnitude.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.alphaThreshold.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(0);
    ctx.fileObj.write<uint8_t>(0);
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int w  = data.width.value  > 0 ? data.width.value  : 1;
    int h  = data.height.value > 0 ? data.height.value : 1;
    int cw = data.cellW.value  > 0 ? data.cellW.value  : 1;
    int ch = data.cellH.value  > 0 ? data.cellH.value  : 1;
    float totalW = (float)(w * cw) * zoom;
    float totalH = (float)(h * ch) * zoom;
    ImVec2 br{originScreen.x + totalW, originScreen.y + totalH};

    // Faint outer rect + cell grid so the user can size the playfield.
    dl->AddRect(originScreen, br, IM_COL32(120, 220, 200, 200), 0.0f, 0, 1.0f);
    ImU32 cellLine = IM_COL32(120, 220, 200, 80);
    float fcw = (float)cw * zoom;
    float fch = (float)ch * zoom;
    for (int x = 1; x < w; x++) {
      float xp = originScreen.x + x * fcw;
      dl->AddLine({xp, originScreen.y}, {xp, br.y}, cellLine, 1.0f);
    }
    for (int y = 1; y < h; y++) {
      float yp = originScreen.y + y * fch;
      dl->AddLine({originScreen.x, yp}, {br.x, yp}, cellLine, 1.0f);
    }

    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
    (void)obj;
  }

  void widgetSize(Object &, Entry &entry, int *outW, int *outH)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (outW) *outW = data.width.value * data.cellW.value;
    if (outH) *outH = data.height.value * data.cellH.value;
  }

  static bool drawTilesetPicker(uint64_t &uuid)
  {
    auto &assets = ctx.project->getAssets();
    const auto &imageList = assets.getTypeEntries(FileType::IMAGE);
    auto *current = (uuid != 0) ? assets.getEntryByUUID(uuid) : nullptr;

    bool changed = false;
    ImGui::PushID("Tileset");

    const float thumbSize = 24.0f;
    const float slotH = thumbSize + 4.0f;
    const float slotW = ImGui::CalcItemWidth();
    ImVec2 slotTL = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Button("##slot", ImVec2(slotW, slotH));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 thumbTL{slotTL.x + 2.0f, slotTL.y + 2.0f};
    ImVec2 thumbBR{thumbTL.x + thumbSize, thumbTL.y + thumbSize};
    SDL_GPUTexture *currentTex = (current && current->texture) ? current->texture->getGPUTex() : nullptr;
    if (currentTex) dl->AddImage(ImTextureID(currentTex), thumbTL, thumbBR);
    else            dl->AddRectFilled(thumbTL, thumbBR, IM_COL32(40, 40, 40, 255));
    const char *displayName = current ? current->name.c_str() : "<None>";
    ImVec2 textPos{thumbBR.x + 6.0f, slotTL.y + (slotH - ImGui::GetFontSize()) * 0.5f};
    dl->AddText(textPos, IM_COL32(220, 220, 220, 255), displayName);

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET")) {
        uint64_t dropped = *(uint64_t*)payload->Data;
        auto *droppedEntry = assets.getEntryByUUID(dropped);
        if (droppedEntry && droppedEntry->type == FileType::IMAGE && uuid != dropped) {
          uuid = dropped;
          changed = true;
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (clicked) ImGui::OpenPopup("grid2d_picker_popup");

    static char filterBuf[64] = "";
    static bool needFocus = false;
    if (clicked) { filterBuf[0] = '\0'; needFocus = true; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 200.0f), ImVec2(420.0f, 480.0f));
    if (ImGui::BeginPopup("grid2d_picker_popup")) {
      if (needFocus) { ImGui::SetKeyboardFocusHere(); needFocus = false; }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##search", "Search...", filterBuf, sizeof(filterBuf));
      ImGui::Separator();

      std::string filter = filterBuf;
      for (char &c : filter) c = (char)std::tolower((unsigned char)c);

      ImGui::BeginChild("##grid2d_picker_list", ImVec2(0, 0));
      for (const auto &e : imageList) {
        if (!filter.empty()) {
          std::string lname = e.name;
          for (char &c : lname) c = (char)std::tolower((unsigned char)c);
          if (lname.find(filter) == std::string::npos) continue;
        }
        ImGui::PushID((const void*)&e);
        bool selected = (e.conf.uuid == uuid);
        if (ImGui::Selectable(e.name.c_str(), selected, 0, ImVec2(0.0f, 28.0f))) {
          if (uuid != e.conf.uuid) { uuid = e.conf.uuid; changed = true; }
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if (changed) Editor::UndoRedo::getHistory().markChanged("Edit Tileset");
    return changed;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::add("Tileset");
      uint64_t &uu = data.tilesetUUID.resolve(obj);
      drawTilesetPicker(uu);
      ImTable::addObjProp("Width (cells)",  data.width);
      ImTable::addObjProp("Height (cells)", data.height);
      ImTable::addObjProp("Cell W (px)",    data.cellW);
      ImTable::addObjProp("Cell H (px)",    data.cellH);
      ImTable::addObjProp("Shake magnitude", data.shakeMagnitude);
      ImTable::addObjProp("Alpha threshold", data.alphaThreshold);
      ImTable::end();
    }
  }
}
