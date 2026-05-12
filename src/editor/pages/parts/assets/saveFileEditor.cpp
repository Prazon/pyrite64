/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "saveFileEditor.h"

#include <cstdio>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../project/assetManager.h"
#include "../../../../project/assets/saveFileAsset.h"
#include "../../../../utils/fs.h"
#include "../../../undoRedo.h"

namespace
{
  using Project::Assets::SaveFileAsset;

  // EEPROM caps mirror P64::Save's MAX_SLOTS_4K / MAX_SLOTS_16K. Used here
  // only to colour the slot-usage label red when the project would overflow.
  constexpr uint32_t SLOT_CAP_4K  = 120;
  constexpr uint32_t SLOT_CAP_16K = 500;

  struct PerAssetState { int selectedField{-1}; std::string renameBuffer{}; };
  std::unordered_map<uint64_t, PerAssetState> &states()
  {
    static std::unordered_map<uint64_t, PerAssetState> s;
    return s;
  }

  void persist(Project::AssetManagerEntry &entry)
  {
    if (!entry.saveFileAsset) return;
    Utils::FS::saveTextFile(entry.path, entry.saveFileAsset->serialize());
  }

  const char* typeName(SaveFileAsset::FieldType t)
  {
    switch (t) {
      case SaveFileAsset::FT_INT:    return "Int";
      case SaveFileAsset::FT_FLOAT:  return "Float";
      case SaveFileAsset::FT_BOOL:   return "Bool";
      case SaveFileAsset::FT_STRING: return "String";
      case SaveFileAsset::FT_VEC2:   return "Vec2";
      case SaveFileAsset::FT_VEC3:   return "Vec3";
    }
    return "?";
  }

  ImU32 typeColor(SaveFileAsset::FieldType t)
  {
    switch (t) {
      case SaveFileAsset::FT_INT:    return IM_COL32( 77, 204, 217, 255);
      case SaveFileAsset::FT_FLOAT:  return IM_COL32(115, 217,  77, 255);
      case SaveFileAsset::FT_BOOL:   return IM_COL32(217,  51,  51, 255);
      case SaveFileAsset::FT_STRING: return IM_COL32(166, 166, 166, 255);
      case SaveFileAsset::FT_VEC2:   return IM_COL32(242, 217,  64, 255);
      case SaveFileAsset::FT_VEC3:   return IM_COL32(242, 140,  51, 255);
    }
    return IM_COL32(128,128,128,255);
  }

  // Project-wide slot total across every SAVE_FILE asset. Used in the header
  // label so the user can see at a glance whether the EEPROM budget fits.
  uint32_t projectTotalSlots()
  {
    if (!ctx.project) return 0;
    uint32_t total = 0;
    for (const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::SAVE_FILE)) {
      if (e.saveFileAsset) total += e.saveFileAsset->slotsUsed();
    }
    return total;
  }

  void drawHeader(Project::AssetManagerEntry &entry)
  {
    auto &asset = *entry.saveFileAsset;

    ImGui::TextDisabled("%s  Save File Schema", ICON_MDI_CONTENT_SAVE_OUTLINE);
    ImGui::Separator();

    char nameBuf[96]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", asset.groupName.c_str());
    ImGui::TextUnformatted("Group Name (C++ namespace)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##groupName", nameBuf, sizeof(nameBuf))) {
      // Sanitize on the fly: only [A-Za-z0-9_], no leading digit.
      std::string g;
      g.reserve(sizeof(nameBuf));
      for (char c : std::string(nameBuf)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_') {
          g.push_back(c);
        }
      }
      if (!g.empty() && g[0] >= '0' && g[0] <= '9') g.insert(g.begin(), '_');
      if (g != asset.groupName) {
        Editor::UndoRedo::getHistory().markChanged("Rename Save Group");
        asset.groupName = std::move(g);
        persist(entry);
      }
    }

    uint32_t mySlots    = asset.slotsUsed();
    uint32_t totalSlots = projectTotalSlots();
    ImGui::Text("This group: %u slots   Project total: %u / %u (4K) / %u (16K)",
      mySlots, totalSlots, SLOT_CAP_4K, SLOT_CAP_16K);
    if (totalSlots > SLOT_CAP_16K) {
      ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),
        "Project exceeds 16K EEPROM cap. Build will fail.");
    } else if (totalSlots > SLOT_CAP_4K) {
      ImGui::TextColored(ImVec4(1,0.8f,0.4f,1),
        "Project exceeds 4K EEPROM cap. Game must run on a 16K cart.");
    }
  }

  void drawFieldsPanel(Project::AssetManagerEntry &entry, PerAssetState &state)
  {
    auto &asset  = *entry.saveFileAsset;
    auto &fields = asset.fields;

    ImGui::PushID("savefile_fields");
    if (ImGui::SmallButton(ICON_MDI_PLUS " Add Field")) {
      SaveFileAsset::Field f{};
      f.name = "newField" + std::to_string(fields.size() + 1);
      f.type = SaveFileAsset::FT_INT;
      Editor::UndoRedo::getHistory().markChanged("Add Save Field");
      fields.push_back(std::move(f));
      state.selectedField = (int)fields.size() - 1;
      persist(entry);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu)", fields.size());

    if (fields.empty()) {
      ImGui::TextDisabled("(no fields yet)");
      ImGui::PopID();
      return;
    }

    int pendingDelete = -1;

    for (size_t i = 0; i < fields.size(); ++i) {
      auto &f = fields[i];
      ImGui::PushID((int)i);
      bool sel = (state.selectedField == (int)i);

      constexpr float PILL_W = 18.0f;
      constexpr float PILL_H = 10.0f;
      constexpr float PILL_X =  6.0f;
      constexpr float TEXT_GAP = 8.0f;
      const float rowH  = ImGui::GetFrameHeight();
      const float lineH = ImGui::GetTextLineHeight();
      ImVec2 rowPos = ImGui::GetCursorScreenPos();

      if (ImGui::Selectable("##fieldRow", sel,
            ImGuiSelectableFlags_AllowOverlap)) {
        state.selectedField = (int)i;
      }

      ImDrawList *dl = ImGui::GetWindowDrawList();
      const float midY  = rowPos.y + rowH * 0.5f;
      const float pillY = midY - PILL_H * 0.5f;
      ImVec2 pmin{rowPos.x + PILL_X, pillY};
      ImVec2 pmax{pmin.x + PILL_W,   pillY + PILL_H};
      dl->AddRectFilled(pmin, pmax, typeColor(f.type), PILL_H * 0.5f);
      dl->AddRect      (pmin, pmax, IM_COL32(0,0,0,160), PILL_H * 0.5f);

      ImVec2 textPos{pmax.x + TEXT_GAP, midY - lineH * 0.5f};
      dl->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text),
        f.name.empty() ? "(unnamed)" : f.name.c_str());

      if (ImGui::BeginPopupContextItem("##fieldCtx")) {
        if (ImGui::MenuItem(ICON_MDI_DELETE " Delete")) {
          pendingDelete = (int)i;
        }
        ImGui::EndPopup();
      }

      ImGui::SameLine(ImGui::GetContentRegionAvail().x
                      + ImGui::GetCursorPosX() - 60.0f);
      ImGui::TextDisabled("%s", typeName(f.type));
      ImGui::PopID();
    }

    if (pendingDelete >= 0 && pendingDelete < (int)fields.size()) {
      Editor::UndoRedo::getHistory().markChanged("Delete Save Field");
      fields.erase(fields.begin() + pendingDelete);
      if (state.selectedField == pendingDelete) state.selectedField = -1;
      else if (state.selectedField > pendingDelete) --state.selectedField;
      persist(entry);
    }
    ImGui::PopID();
  }

  void drawFieldDetails(Project::AssetManagerEntry &entry, PerAssetState &state)
  {
    auto &asset  = *entry.saveFileAsset;
    auto &fields = asset.fields;
    if (state.selectedField < 0 || state.selectedField >= (int)fields.size()) {
      ImGui::TextDisabled("(no field selected)");
      return;
    }
    auto &f = fields[state.selectedField];

    auto stateBefore = asset.serialize();

    ImGui::TextDisabled("%s  Field", ICON_MDI_VARIABLE);
    ImGui::Separator();

    char nameBuf[96]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", f.name.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##fname", nameBuf, sizeof(nameBuf))) {
      // Same C++ identifier rule the build relies on. Generated header is
      // unhappy with spaces or punctuation.
      std::string g;
      g.reserve(sizeof(nameBuf));
      for (char c : std::string(nameBuf)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_') {
          g.push_back(c);
        }
      }
      if (!g.empty() && g[0] >= '0' && g[0] <= '9') g.insert(g.begin(), '_');
      f.name = g;
    }

    static const char* tNames[] = { "Int", "Float", "Bool", "String", "Vec2", "Vec3" };
    int tIdx = (int)f.type;
    ImGui::TextUnformatted("Type");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##ftype", &tIdx, tNames, IM_ARRAYSIZE(tNames))) {
      f.type = (SaveFileAsset::FieldType)tIdx;
    }

    if (f.type == SaveFileAsset::FT_STRING) {
      int n = (int)f.stringLen;
      ImGui::TextUnformatted("Max Length (chars)");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderInt("##slen", &n, 1, 32)) {
        if (n < 1)  n = 1;
        if (n > 32) n = 32;
        f.stringLen = (uint32_t)n;
      }
    }

    ImGui::TextUnformatted("Default");
    ImGui::SetNextItemWidth(-1);
    switch (f.type) {
      case SaveFileAsset::FT_INT:
        ImGui::DragInt("##fdef", &f.defInt);
        break;
      case SaveFileAsset::FT_FLOAT:
        ImGui::DragFloat("##fdef", &f.defFloat, 0.01f);
        break;
      case SaveFileAsset::FT_BOOL:
        ImGui::Checkbox("##fdef", &f.defBool);
        break;
      case SaveFileAsset::FT_STRING: {
        std::string buf = f.defString;
        buf.resize(33, '\0');
        if (ImGui::InputText("##fdef", buf.data(), std::min<size_t>(buf.size(), f.stringLen + 1))) {
          f.defString = buf.c_str();
        }
        break;
      }
      case SaveFileAsset::FT_VEC2:
        ImGui::DragFloat2("##fdef", f.defVec, 0.01f);
        break;
      case SaveFileAsset::FT_VEC3:
        ImGui::DragFloat3("##fdef", f.defVec, 0.01f);
        break;
    }

    ImGui::TextDisabled("Slot cost: %u", SaveFileAsset::fieldSlotCount(f));

    if (stateBefore != asset.serialize()) {
      Editor::UndoRedo::getHistory().markChanged("Edit Save Field");
      persist(entry);
    }
  }
}

void Editor::SaveFileEditor::draw(Project::AssetManagerEntry &entry)
{
  if (entry.type != Project::FileType::SAVE_FILE || !entry.saveFileAsset) return;

  auto &state = states()[entry.saveFileAsset->uuid];
  drawHeader(entry);

  const float listW = ImGui::GetContentRegionAvail().x * 0.45f;
  if (ImGui::BeginChild("##saveList", ImVec2(listW, 280), true)) {
    drawFieldsPanel(entry, state);
  }
  ImGui::EndChild();

  ImGui::SameLine();
  if (ImGui::BeginChild("##saveDetails", ImVec2(0, 280), true)) {
    drawFieldDetails(entry, state);
  }
  ImGui::EndChild();
}
