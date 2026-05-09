/**
* Editor-side NinePatch2D component.
* Pairs with engine: n64/engine/src/scene/components/ninePatch2D.cpp
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

namespace Project::Component::NinePatch2D
{
  struct Data
  {
    PROP_U64(spriteUUID);
    PROP_S32(width);
    PROP_S32(height);
    PROP_S32(borderL);
    PROP_S32(borderR);
    PROP_S32(borderT);
    PROP_S32(borderB);
    PROP_S32(tintR);
    PROP_S32(tintG);
    PROP_S32(tintB);
    PROP_S32(tintA);
    PROP_S32(alphaThreshold);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->width.value  = 64;
    data->height.value = 32;
    data->borderL.value = 4;
    data->borderR.value = 4;
    data->borderT.value = 4;
    data->borderB.value = 4;
    data->tintR.value = 255; data->tintG.value = 255;
    data->tintB.value = 255; data->tintA.value = 255;
    data->alphaThreshold.value = 1;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spriteUUID);
    b.set(data.width);
    b.set(data.height);
    b.set(data.borderL);
    b.set(data.borderR);
    b.set(data.borderT);
    b.set(data.borderB);
    b.set(data.tintR);
    b.set(data.tintG);
    b.set(data.tintB);
    b.set(data.tintA);
    b.set(data.alphaThreshold);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spriteUUID);
    Utils::JSON::readProp(doc, data->width, 64);
    Utils::JSON::readProp(doc, data->height, 32);
    Utils::JSON::readProp(doc, data->borderL, 4);
    Utils::JSON::readProp(doc, data->borderR, 4);
    Utils::JSON::readProp(doc, data->borderT, 4);
    Utils::JSON::readProp(doc, data->borderB, 4);
    Utils::JSON::readProp(doc, data->tintR, 255);
    Utils::JSON::readProp(doc, data->tintG, 255);
    Utils::JSON::readProp(doc, data->tintB, 255);
    Utils::JSON::readProp(doc, data->tintA, 255);
    Utils::JSON::readProp(doc, data->alphaThreshold, 1);
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
    if (data.spriteUUID.value != 0) {
      auto res = ctx.assetUUIDToIdx.find(data.spriteUUID.value);
      if (res != ctx.assetUUIDToIdx.end()) spriteIdx = res->second;
      else Utils::Logger::log(
        "NinePatch2D: sprite UUID not found: " + std::to_string(data.spriteUUID.value),
        Utils::Logger::LEVEL_WARN
      );
    }

    // Layout MUST match engine InitData in ninePatch2D.cpp (sizeof == 16)
    ctx.fileObj.write<uint16_t>(spriteIdx);
    ctx.fileObj.write<uint16_t>((uint16_t)data.width.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.height.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(clamp8(data.borderL.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.borderR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.borderT.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.borderB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintR.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintG.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintB.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.tintA.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(clamp8(data.alphaThreshold.resolve(obj.propOverrides)));
    ctx.fileObj.write<uint8_t>(0);
  }

  // Local image picker, scoped to NinePatch2D so its popup ID does not
  // collide with other components' pickers when the inspector shows several
  // sprite-bearing components on the same Object.
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

    if (clicked) ImGui::OpenPopup("ninepatch_picker_popup");

    static char filterBuf[64] = "";
    static bool needFocus = false;
    if (clicked) { filterBuf[0] = '\0'; needFocus = true; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 200.0f), ImVec2(420.0f, 480.0f));
    if (ImGui::BeginPopup("ninepatch_picker_popup")) {
      if (needFocus) { ImGui::SetKeyboardFocusHere(); needFocus = false; }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##search", "Search...", filterBuf, sizeof(filterBuf));
      ImGui::Separator();

      std::string filter = filterBuf;
      for (char &c : filter) c = (char)std::tolower((unsigned char)c);

      ImGui::BeginChild("##ninepatch_picker_list", ImVec2(0, 0));
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

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    int  pxW = data.width.value  > 0 ? data.width.value  : 1;
    int  pxH = data.height.value > 0 ? data.height.value : 1;
    float w = (float)pxW * zoom;
    float h = (float)pxH * zoom;
    ImVec2 br{originScreen.x + w, originScreen.y + h};

    auto *e = (data.spriteUUID.value != 0)
                ? ctx.project->getAssets().getEntryByUUID(data.spriteUUID.value)
                : nullptr;
    SDL_GPUTexture *gpu = (e && e->texture) ? e->texture->getGPUTex() : nullptr;

    if (!gpu) {
      // No sprite assigned: stroke an outline so the panel stays pickable
      // and the user can see where it sits while authoring.
      dl->AddRect(originScreen, br, IM_COL32(180, 180, 180, 220));
      dl->AddText({originScreen.x + 2, originScreen.y + 2},
                  IM_COL32(180, 180, 180, 200), "9-Patch");
      if (outMin) *outMin = originScreen;
      if (outMax) *outMax = br;
      return;
    }

    int sw = (int)e->texture->getWidth();
    int sh = (int)e->texture->getHeight();
    int bL = data.borderL.value, bR = data.borderR.value;
    int bT = data.borderT.value, bB = data.borderB.value;
    if (bL < 0) bL = 0;
    if (bR < 0) bR = 0;
    if (bT < 0) bT = 0;
    if (bB < 0) bB = 0;
    if (bL + bR > sw) { bL = sw / 2; bR = sw - bL; }
    if (bT + bB > sh) { bT = sh / 2; bB = sh - bT; }

    int srcMidW = sw - bL - bR; if (srcMidW < 0) srcMidW = 0;
    int srcMidH = sh - bT - bB; if (srcMidH < 0) srcMidH = 0;
    int dstMidW = pxW - bL - bR; if (dstMidW < 0) dstMidW = 0;
    int dstMidH = pxH - bT - bB; if (dstMidH < 0) dstMidH = 0;

    ImU32 tint = IM_COL32(clamp8(data.tintR.value), clamp8(data.tintG.value),
                          clamp8(data.tintB.value), clamp8(data.tintA.value));

    auto slice = [&](int sx, int sy, int sszW, int sszH,
                     int dx, int dy, int dszW, int dszH)
    {
      if (sszW <= 0 || sszH <= 0 || dszW <= 0 || dszH <= 0) return;
      ImVec2 a{originScreen.x + (float)dx * zoom, originScreen.y + (float)dy * zoom};
      ImVec2 b{a.x + (float)dszW * zoom,           a.y + (float)dszH * zoom};
      ImVec2 uv0{(float)sx / (float)sw, (float)sy / (float)sh};
      ImVec2 uv1{(float)(sx + sszW) / (float)sw, (float)(sy + sszH) / (float)sh};
      dl->AddImage(ImTextureID(gpu), a, b, uv0, uv1, tint);
    };

    int xL = 0,  xM = bL,           xR = bL + dstMidW;
    int yT = 0,  yM = bT,           yB = bT + dstMidH;

    slice(0,        0,        bL,      bT,      xL, yT, bL,      bT);
    slice(bL,       0,        srcMidW, bT,      xM, yT, dstMidW, bT);
    slice(sw - bR,  0,        bR,      bT,      xR, yT, bR,      bT);

    slice(0,        bT,       bL,      srcMidH, xL, yM, bL,      dstMidH);
    slice(bL,       bT,       srcMidW, srcMidH, xM, yM, dstMidW, dstMidH);
    slice(sw - bR,  bT,       bR,      srcMidH, xR, yM, bR,      dstMidH);

    slice(0,        sh - bB,  bL,      bB,      xL, yB, bL,      bB);
    slice(bL,       sh - bB,  srcMidW, bB,      xM, yB, dstMidW, bB);
    slice(sw - bR,  sh - bB,  bR,      bB,      xR, yB, bR,      bB);

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

      ImTable::add("Sprite");
      uint64_t &spriteUUID = data.spriteUUID.resolve(obj);
      drawImagePicker("Sprite", spriteUUID);

      ImTable::addObjProp("Width",  data.width);
      ImTable::addObjProp("Height", data.height);
      ImTable::addObjProp("Border L", data.borderL);
      ImTable::addObjProp("Border R", data.borderR);
      ImTable::addObjProp("Border T", data.borderT);
      ImTable::addObjProp("Border B", data.borderB);
      ImTable::addObjProp("Tint R", data.tintR);
      ImTable::addObjProp("Tint G", data.tintG);
      ImTable::addObjProp("Tint B", data.tintB);
      ImTable::addObjProp("Tint A", data.tintA);
      ImTable::addObjProp("Alpha Threshold", data.alphaThreshold);

      ImTable::end();
    }
  }
}
