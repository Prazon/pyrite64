/**
* Editor-side Sprite2D component.
* Pairs with engine: n64/engine/src/scene/components/sprite2D.cpp
*
* Authoring concerns: parented Object must be flagged as 2D (under a Canvas)
* for the engine's 2D-pass loop to ever call draw(). Inspector exposes the
* sprite-asset picker, sheet/cell breakdown, tint, and pixel scale.
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

namespace Project::Component::Sprite2D
{
  struct Data
  {
    PROP_U64(spriteUUID);
    PROP_S32(cellW);
    PROP_S32(cellH);
    PROP_S32(frame);
    PROP_BOOL(flipX);
    PROP_S32(alphaThreshold);
    PROP_S32(tintR);
    PROP_S32(tintG);
    PROP_S32(tintB);
    PROP_S32(tintA);
    PROP_FLOAT(pixelScale); // editor-side as float; runtime stores q4.4
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->cellW.value = 0;
    data->cellH.value = 0;
    data->frame.value = 0;
    data->alphaThreshold.value = 100;
    data->tintR.value = 255;
    data->tintG.value = 255;
    data->tintB.value = 255;
    data->tintA.value = 255;
    data->pixelScale.value = 1.0f;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spriteUUID);
    b.set(data.cellW);
    b.set(data.cellH);
    b.set(data.frame);
    b.set(data.flipX);
    b.set(data.alphaThreshold);
    b.set(data.tintR);
    b.set(data.tintG);
    b.set(data.tintB);
    b.set(data.tintA);
    b.set(data.pixelScale);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spriteUUID);
    Utils::JSON::readProp(doc, data->cellW, 0);
    Utils::JSON::readProp(doc, data->cellH, 0);
    Utils::JSON::readProp(doc, data->frame, 0);
    Utils::JSON::readProp(doc, data->flipX);
    Utils::JSON::readProp(doc, data->alphaThreshold, 100);
    Utils::JSON::readProp(doc, data->tintR, 255);
    Utils::JSON::readProp(doc, data->tintG, 255);
    Utils::JSON::readProp(doc, data->tintB, 255);
    Utils::JSON::readProp(doc, data->tintA, 255);
    Utils::JSON::readProp(doc, data->pixelScale, 1.0f);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    uint16_t spriteIdx = 0xFFFF;
    if (data.spriteUUID.value != 0) {
      auto res = ctx.assetUUIDToIdx.find(data.spriteUUID.value);
      if (res != ctx.assetUUIDToIdx.end()) spriteIdx = res->second;
      else Utils::Logger::log(
        "Sprite2D: sprite UUID not found: " + std::to_string(data.spriteUUID.value),
        Utils::Logger::LEVEL_WARN
      );
    }

    // Layout MUST match engine's InitData in sprite2D.cpp
    ctx.fileObj.write<uint16_t>(spriteIdx);
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellW.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellH.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.frame.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(data.flipX.resolve(obj.propOverrides) ? 1 : 0);

    int alpha = data.alphaThreshold.resolve(obj.propOverrides);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    ctx.fileObj.write<uint8_t>((uint8_t)alpha);

    auto clamp8 = [](int v) {
      if (v < 0) return (uint8_t)0;
      if (v > 255) return (uint8_t)255;
      return (uint8_t)v;
    };
    ctx.fileObj.write<uint8_t>(clamp8(data.tintR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintA.resolve(obj.propOverrides)));

    float ps = data.pixelScale.resolve(obj.propOverrides);
    int q = (int)(ps * 16.0f + 0.5f);
    if (q < 1) q = 16; // 0 was previously "auto" in spriteBillboard; here it
                      // collapses to 1.0x since 2D has no camera distance.
    if (q > 255) q = 255;
    ctx.fileObj.write<uint8_t>((uint8_t)q);
    ctx.fileObj.write<uint8_t>(0); // pad
  }

  // Asset picker — same shape as compSpriteBillboard's. Kept local rather
  // than extracted because it's only used in two places and pulling it out
  // adds another header for one helper.
  static bool drawImagePicker(const char *fieldName, uint64_t &uuid)
  {
    auto &assets = ctx.project->getAssets();
    const auto &imageList = assets.getTypeEntries(FileType::IMAGE);
    auto *current = (uuid != 0) ? assets.getEntryByUUID(uuid) : nullptr;

    ImGui::PushID(fieldName);
    bool changed = false;

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

    if (clicked) ImGui::OpenPopup("sprite2d_picker_popup");

    static char filterBuf[64] = "";
    static bool needFocus = false;
    if (clicked) { filterBuf[0] = '\0'; needFocus = true; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 200.0f), ImVec2(420.0f, 480.0f));
    if (ImGui::BeginPopup("sprite2d_picker_popup")) {
      if (needFocus) { ImGui::SetKeyboardFocusHere(); needFocus = false; }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##search", "Search...", filterBuf, sizeof(filterBuf));
      ImGui::Separator();

      std::string filter = filterBuf;
      for (char &c : filter) c = (char)std::tolower((unsigned char)c);

      ImGui::BeginChild("##sprite2d_picker_list", ImVec2(0, 0));
      const float rowH = 36.0f;
      const float rowThumb = 32.0f;
      for (const auto &e : imageList) {
        if (!filter.empty()) {
          std::string lname = e.name;
          for (char &c : lname) c = (char)std::tolower((unsigned char)c);
          if (lname.find(filter) == std::string::npos) continue;
        }

        ImGui::PushID((const void*)&e);
        bool selected = (e.conf.uuid == uuid);
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        bool rowClicked = ImGui::Selectable("##row", selected,
          ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowH));

        ImDrawList *rdl = ImGui::GetWindowDrawList();
        ImVec2 rt{rowStart.x + 2.0f, rowStart.y + 2.0f};
        ImVec2 rb{rt.x + rowThumb, rt.y + rowThumb};
        SDL_GPUTexture *tex = e.texture ? e.texture->getGPUTex() : nullptr;
        if (tex) rdl->AddImage(ImTextureID(tex), rt, rb);
        else     rdl->AddRectFilled(rt, rb, IM_COL32(40, 40, 40, 255));
        ImVec2 nm{rb.x + 6.0f, rowStart.y + (rowH - ImGui::GetFontSize()) * 0.5f};
        rdl->AddText(nm, IM_COL32(220, 220, 220, 255), e.name.c_str());

        if (rowClicked) {
          if (uuid != e.conf.uuid) { uuid = e.conf.uuid; changed = true; }
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::EndPopup();
    }

    ImGui::PopID();

    if (changed) Editor::UndoRedo::getHistory().markChanged(std::string("Edit ") + fieldName);
    return changed;
  }

  static void autoFillCellSize(Data &data, uint64_t newUUID)
  {
    if (newUUID == 0) return;
    if (data.cellW.value != 0 || data.cellH.value != 0) return;
    auto *assetEntry = ctx.project->getAssets().getEntryByUUID(newUUID);
    if (!assetEntry || !assetEntry->texture) return;
    data.cellW.value = (int32_t)assetEntry->texture->getWidth();
    data.cellH.value = (int32_t)assetEntry->texture->getHeight();
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto *e = (data.spriteUUID.value != 0)
                ? ctx.project->getAssets().getEntryByUUID(data.spriteUUID.value)
                : nullptr;

    if (!e || !e->texture) {
      // Placeholder rect — keeps the object pickable in the 2D viewport
      // even when no sprite asset is bound yet.
      float w = (data.cellW.value > 0) ? (float)data.cellW.value : 16.0f;
      float h = (data.cellH.value > 0) ? (float)data.cellH.value : 16.0f;
      ImVec2 br{originScreen.x + w * zoom, originScreen.y + h * zoom};
      dl->AddRect(originScreen, br, IM_COL32(180, 180, 180, 200));
      dl->AddText({originScreen.x + 2, originScreen.y + 2},
                  IM_COL32(180, 180, 180, 200), "Sprite");
      if (outMin) *outMin = originScreen;
      if (outMax) *outMax = br;
      return;
    }

    SDL_GPUTexture *gpu = e->texture->getGPUTex();
    if (!gpu) return;

    float texW = (float)e->texture->getWidth();
    float texH = (float)e->texture->getHeight();
    float cellW = (data.cellW.value > 0) ? (float)data.cellW.value : texW;
    float cellH = (data.cellH.value > 0) ? (float)data.cellH.value : texH;
    int sheetCols = (cellW > 0) ? (int)(texW / cellW) : 1;
    if (sheetCols < 1) sheetCols = 1;
    int frame = data.frame.value;
    int cellX = sheetCols > 0 ? (frame % sheetCols) : 0;
    int cellY = sheetCols > 0 ? (frame / sheetCols) : 0;

    float ps = (data.pixelScale.value > 0.0f) ? data.pixelScale.value : 1.0f;
    float drawW = cellW * ps * zoom;
    float drawH = cellH * ps * zoom;

    ImVec2 br{originScreen.x + drawW, originScreen.y + drawH};
    ImVec2 uv0{(cellX * cellW) / texW,       (cellY * cellH) / texH};
    ImVec2 uv1{((cellX + 1) * cellW) / texW, ((cellY + 1) * cellH) / texH};
    if (data.flipX.value) std::swap(uv0.x, uv1.x);

    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    ImU32 tint = IM_COL32(
      clamp8(data.tintR.value), clamp8(data.tintG.value),
      clamp8(data.tintB.value), clamp8(data.tintA.value));

    dl->AddImage(ImTextureID(gpu), originScreen, br, uv0, uv1, tint);
    if (outMin) *outMin = originScreen;
    if (outMax) *outMax = br;
    (void)obj;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      ImTable::add("Sprite");
      uint64_t &spriteUUID = data.spriteUUID.resolve(obj);
      if (drawImagePicker("Sprite", spriteUUID)) {
        autoFillCellSize(data, spriteUUID);
      }

      ImTable::addObjProp("Cell W (0=auto)", data.cellW);
      ImTable::addObjProp("Cell H (0=auto)", data.cellH);
      ImTable::addObjProp("Frame", data.frame);
      ImTable::addObjProp("Flip X", data.flipX);
      ImTable::addObjProp("Alpha Threshold", data.alphaThreshold);
      ImTable::addObjProp("Tint R", data.tintR);
      ImTable::addObjProp("Tint G", data.tintG);
      ImTable::addObjProp("Tint B", data.tintB);
      ImTable::addObjProp("Tint A", data.tintA);
      ImTable::addObjProp("Pixel Scale", data.pixelScale);

      ImTable::end();
    }
  }
}
