/**
* Editor-side PaperSprite component.
* Pairs with engine: n64/engine/src/scene/components/paperSprite.cpp
* Identical wire format to SpriteBillboard: the runtime difference is purely
* in the engine draw path (z-test + z-write at the world anchor's depth), so
* the editor schema, inspector and preview mirror compSpriteBillboard.cpp.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/colors.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../utils/meshGen.h"

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/ext/matrix_projection.hpp"

#include <cctype>
#include <string>

namespace Project::Component::PaperSprite
{
  struct Data
  {
    PROP_U64(spriteUUID);
    PROP_S32(cellW);
    PROP_S32(cellH);
    PROP_S32(frame);
    PROP_S32(pivotX);
    PROP_S32(pivotY);
    PROP_BOOL(flipX);
    PROP_S32(layerIdx2D);
    PROP_FLOAT(pixelScale);     // 0 = auto from camera distance
    PROP_S32(alphaThreshold);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->cellW.value = 0;
    data->cellH.value = 0;
    data->frame.value = 0;
    data->pivotX.value = 0;
    data->pivotY.value = 0;
    data->layerIdx2D.value = 0;
    data->pixelScale.value = 1.0f;
    data->alphaThreshold.value = 100;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spriteUUID);
    b.set(data.cellW);
    b.set(data.cellH);
    b.set(data.frame);
    b.set(data.pivotX);
    b.set(data.pivotY);
    b.set(data.flipX);
    b.set(data.layerIdx2D);
    b.set(data.pixelScale);
    b.set(data.alphaThreshold);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spriteUUID);
    Utils::JSON::readProp(doc, data->cellW, 0);
    Utils::JSON::readProp(doc, data->cellH, 0);
    Utils::JSON::readProp(doc, data->frame, 0);
    Utils::JSON::readProp(doc, data->pivotX, 0);
    Utils::JSON::readProp(doc, data->pivotY, 0);
    Utils::JSON::readProp(doc, data->flipX);
    Utils::JSON::readProp(doc, data->layerIdx2D, 0);
    Utils::JSON::readProp(doc, data->pixelScale, 1.0f);
    Utils::JSON::readProp(doc, data->alphaThreshold, 100);
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
        "PaperSprite: sprite UUID not found: " + std::to_string(data.spriteUUID.value),
        Utils::Logger::LEVEL_WARN
      );
    }

    // Layout MUST match engine's InitData in paperSprite.cpp (which is the
    // same shape as SpriteBillboard's).
    ctx.fileObj.write<uint16_t>(spriteIdx);
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellW.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellH.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.frame.resolve(obj.propOverrides));
    ctx.fileObj.write<int16_t>((int16_t)data.pivotX.resolve(obj.propOverrides));
    ctx.fileObj.write<int16_t>((int16_t)data.pivotY.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(data.flipX.resolve(obj.propOverrides) ? 1 : 0);
    ctx.fileObj.write<uint8_t>((uint8_t)data.layerIdx2D.resolve(obj.propOverrides));

    float ps = data.pixelScale.resolve(obj.propOverrides);
    uint8_t psQ = 0;
    if (ps > 0.0f) {
      int q = (int)(ps * 16.0f + 0.5f);
      if (q < 1) q = 1;
      if (q > 255) q = 255;
      psQ = (uint8_t)q;
    }
    ctx.fileObj.write<uint8_t>(psQ);

    int alpha = data.alphaThreshold.resolve(obj.propOverrides);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    ctx.fileObj.write<uint8_t>((uint8_t)alpha);
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

  // Searchable image-asset picker with thumbnails (clone of the
  // SpriteBillboard inspector picker; for sprites you really need to see the
  // image to pick correctly).
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

    if (clicked) ImGui::OpenPopup("image_picker_popup");

    static char filterBuf[64] = "";
    static bool needFocus = false;
    if (clicked) { filterBuf[0] = '\0'; needFocus = true; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 200.0f), ImVec2(420.0f, 480.0f));
    if (ImGui::BeginPopup("image_picker_popup")) {
      if (needFocus) { ImGui::SetKeyboardFocusHere(); needFocus = false; }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##search", "Search...", filterBuf, sizeof(filterBuf));
      ImGui::Separator();

      std::string filter = filterBuf;
      for (char &c : filter) c = (char)std::tolower((unsigned char)c);

      ImGui::BeginChild("##image_picker_list", ImVec2(0, 0));
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
      ImTable::addObjProp("Pivot X (px)", data.pivotX);
      ImTable::addObjProp("Pivot Y (px)", data.pivotY);
      ImTable::addObjProp("Flip X", data.flipX);
      ImTable::addObjProp("2D Layer Idx", data.layerIdx2D);
      ImTable::addObjProp("Pixel Scale (0=auto)", data.pixelScale);
      ImTable::addObjProp("Alpha Threshold", data.alphaThreshold);

      ImTable::end();
    }
  }

  void draw3D(Object &obj, Entry &entry, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto *assetEntry = (data.spriteUUID.value != 0)
        ? ctx.project->getAssets().getEntryByUUID(data.spriteUUID.value)
        : nullptr;

    if (!assetEntry || assetEntry->type != FileType::IMAGE || !assetEntry->texture) {
      glm::u8vec4 col{0xCC, 0xCC, 0xFF, 0xFF};
      if (Editor::activeViewportSelection().isSelected(obj.uuid)) col = Utils::Colors::kSelectionTint;
      Utils::Mesh::addSprite(*vp.getSprites(), obj.pos.resolve(obj.propOverrides),
                             obj.uuid, 4, col);
      return;
    }

    SDL_GPUTexture *gpuTex = assetEntry->texture->getGPUTex();
    if (!gpuTex) return;

    const float texW = (float)assetEntry->texture->getWidth();
    const float texH = (float)assetEntry->texture->getHeight();
    if (texW <= 0.0f || texH <= 0.0f) return;

    const float cellW = data.cellW.resolve(obj.propOverrides) > 0
                          ? (float)data.cellW.resolve(obj.propOverrides) : texW;
    const float cellH = data.cellH.resolve(obj.propOverrides) > 0
                          ? (float)data.cellH.resolve(obj.propOverrides) : texH;
    const int sheetCols = (int)(texW / cellW);
    const int frame = data.frame.resolve(obj.propOverrides);
    const int cellX = sheetCols > 0 ? (frame % sheetCols) : 0;
    const int cellY = sheetCols > 0 ? (frame / sheetCols) : 0;

    const float pixelScale = data.pixelScale.resolve(obj.propOverrides) > 0.0f
                              ? data.pixelScale.resolve(obj.propOverrides) : 1.0f;

    const float worldPerPx = pixelScale;

    glm::vec4 sizeAndPivot{
      cellW, cellH,
      (float)data.pivotX.resolve(obj.propOverrides),
      (float)data.pivotY.resolve(obj.propOverrides)
    };
    glm::vec4 uvRect{
      (cellX * cellW) / texW,
      (cellY * cellH) / texH,
      ((cellX + 1) * cellW) / texW,
      ((cellY + 1) * cellH) / texH
    };

    if (data.flipX.resolve(obj.propOverrides)) {
      std::swap(uvRect.x, uvRect.z);
    }

    glm::vec4 mode{ worldPerPx, Editor::activeViewportSelection().isSelected(obj.uuid) ? 1.0f : 0.0f, 0.0f, 0.0f };

    vp.addBillboardQuad(obj.pos.resolve(obj.propOverrides), obj.uuid,
                        gpuTex, sizeAndPivot, uvRect, mode);

    if (Editor::activeViewportSelection().isSelected(obj.uuid)) {
      Utils::Mesh::addSprite(*vp.getSprites(), obj.pos.resolve(obj.propOverrides),
                             obj.uuid, 4, Utils::Colors::kSelectionTint);
    }
  }
}
