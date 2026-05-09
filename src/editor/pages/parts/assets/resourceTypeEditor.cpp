/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "resourceTypeEditor.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../project/assetManager.h"
#include "../../../../project/scene/varDef.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/hash.h"
#include "../../../undoRedo.h"

namespace
{
  // Per-asset selection: which field row is open in the details panel.
  // Keyed by RESOURCE_TYPE asset uuid so switching assets keeps each one's
  // selection independent.
  struct PerAssetState { int selectedField{-1}; std::string renameBuffer{}; };
  std::unordered_map<uint64_t, PerAssetState>& states()
  {
    static std::unordered_map<uint64_t, PerAssetState> s;
    return s;
  }

  void persist(Project::AssetManagerEntry &entry)
  {
    if (!entry.resourceType) return;
    Utils::FS::saveTextFile(entry.path, entry.resourceType->serialize());
  }

  void seedDefault(Project::VarDef &v)
  {
    v.defaultValue = GenericValue{};
    v.typeArg = 0;
    switch (v.kind) {
      case Project::VarKind::INT:        v.defaultValue.set<int32_t>(0); break;
      case Project::VarKind::FLOAT:      v.defaultValue.set<float>(0.0f); break;
      case Project::VarKind::BOOL:       v.defaultValue.set<bool>(false); break;
      case Project::VarKind::VEC3:       v.defaultValue.set<glm::vec3>({0,0,0}); break;
      case Project::VarKind::QUAT:       v.defaultValue.set<glm::quat>(glm::quat{1,0,0,0}); break;
      case Project::VarKind::OBJECT_REF:
      case Project::VarKind::PREFAB_REF:
      case Project::VarKind::ASSET_REF:  v.defaultValue.set<uint64_t>(0); break;
    }
  }

  void drawFieldsPanel(Project::AssetManagerEntry &entry, PerAssetState &state)
  {
    auto &fields = entry.resourceType->fields;

    ImGui::PushID("rtype_fields");
    if (ImGui::SmallButton(ICON_MDI_PLUS " Add Field")) {
      Project::VarDef v{};
      v.uuid = Utils::Hash::sha256_64bit(
        std::to_string(rand()) + std::to_string(fields.size())
      );
      v.name = "NewField_" + std::to_string(fields.size() + 1);
      v.kind = Project::VarKind::INT;
      seedDefault(v);
      Editor::UndoRedo::getHistory().markChanged("Add Resource Field");
      fields.push_back(std::move(v));
      state.selectedField = static_cast<int>(fields.size()) - 1;
      persist(entry);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu)", fields.size());

    if (fields.empty()) {
      ImGui::TextDisabled("(no fields yet)");
      ImGui::PopID();
      return;
    }

    static const char* kindShort[] = {
      "int", "float", "bool", "vec3", "quat", "obj", "prefab", "asset",
    };
    static const ImU32 kindCol[] = {
      IM_COL32( 77, 204, 217, 255),
      IM_COL32(115, 217,  77, 255),
      IM_COL32(217,  51,  51, 255),
      IM_COL32(242, 217,  64, 255),
      IM_COL32(242, 140,  51, 255),
      IM_COL32( 77, 140, 242, 255),
      IM_COL32(217,  77, 217, 255),
      IM_COL32(166, 166, 166, 255),
    };

    int pendingDelete = -1;
    int pendingDuplicate = -1;

    for (size_t i = 0; i < fields.size(); ++i) {
      auto &v = fields[i];
      ImGui::PushID(static_cast<int>(i));
      bool sel = (state.selectedField == static_cast<int>(i));

      constexpr float PILL_W = 16.0f;
      constexpr float PILL_H = 10.0f;
      constexpr float PILL_X =  6.0f;
      constexpr float TEXT_GAP = 8.0f;
      const float rowH = ImGui::GetFrameHeight();
      const float lineH = ImGui::GetTextLineHeight();
      ImVec2 rowPos = ImGui::GetCursorScreenPos();

      if (ImGui::Selectable("##fieldRow", sel,
            ImGuiSelectableFlags_AllowOverlap)) {
        state.selectedField = static_cast<int>(i);
      }

      int k = static_cast<int>(v.kind);
      if (k < 0 || k >= IM_ARRAYSIZE(kindCol)) k = 0;
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const float midY  = rowPos.y + rowH * 0.5f;
      const float pillY = midY - PILL_H * 0.5f;
      ImVec2 pmin{rowPos.x + PILL_X, pillY};
      ImVec2 pmax{pmin.x + PILL_W,   pillY + PILL_H};
      dl->AddRectFilled(pmin, pmax, kindCol[k], PILL_H * 0.5f);
      dl->AddRect      (pmin, pmax, IM_COL32(0, 0, 0, 160), PILL_H * 0.5f);

      ImVec2 textPos{pmax.x + TEXT_GAP, midY - lineH * 0.5f};
      dl->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), v.name.c_str());

      if (ImGui::BeginPopupContextItem("##fieldCtx")) {
        if (ImGui::MenuItem(ICON_MDI_RENAME_BOX " Rename")) {
          state.renameBuffer = v.name;
          ImGui::CloseCurrentPopup();
          ImGui::OpenPopup("##fieldRenamePopup");
        }
        if (ImGui::MenuItem(ICON_MDI_CONTENT_DUPLICATE " Duplicate")) {
          pendingDuplicate = static_cast<int>(i);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MDI_DELETE " Delete")) {
          pendingDelete = static_cast<int>(i);
        }
        ImGui::EndPopup();
      }

      if (ImGui::BeginPopup("##fieldRenamePopup")) {
        ImGui::TextUnformatted("Rename Field");
        ImGui::SetKeyboardFocusHere();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", state.renameBuffer.c_str());
        if (ImGui::InputText("##fieldRenameInput", buf, sizeof(buf),
              ImGuiInputTextFlags_EnterReturnsTrue)) {
          state.renameBuffer = buf;
          if (!state.renameBuffer.empty() && state.renameBuffer != v.name) {
            Editor::UndoRedo::getHistory().markChanged("Rename Resource Field");
            v.name = state.renameBuffer;
            persist(entry);
          }
          ImGui::CloseCurrentPopup();
        } else {
          state.renameBuffer = buf;
        }
        ImGui::EndPopup();
      }

      ImGui::SameLine(ImGui::GetContentRegionAvail().x
                      + ImGui::GetCursorPosX() - 60.0f);
      ImGui::TextDisabled("%s", kindShort[k]);
      ImGui::PopID();
    }

    if (pendingDuplicate >= 0 && pendingDuplicate < (int)fields.size()) {
      Project::VarDef copy = fields[pendingDuplicate];
      copy.uuid = Utils::Hash::sha256_64bit(
        std::to_string(rand()) + std::to_string(fields.size()) + "_dup"
      );
      copy.name = copy.name + "_Copy";
      Editor::UndoRedo::getHistory().markChanged("Duplicate Resource Field");
      fields.insert(fields.begin() + pendingDuplicate + 1, std::move(copy));
      state.selectedField = pendingDuplicate + 1;
      persist(entry);
    }
    if (pendingDelete >= 0 && pendingDelete < (int)fields.size()) {
      Editor::UndoRedo::getHistory().markChanged("Delete Resource Field");
      fields.erase(fields.begin() + pendingDelete);
      if (state.selectedField == pendingDelete) {
        state.selectedField = -1;
      } else if (state.selectedField > pendingDelete) {
        --state.selectedField;
      }
      persist(entry);
    }
    ImGui::PopID();
  }

  void drawFieldDetails(Project::AssetManagerEntry &entry, PerAssetState &state)
  {
    auto &fields = entry.resourceType->fields;
    if (state.selectedField < 0 || state.selectedField >= (int)fields.size()) {
      ImGui::TextDisabled("(no field selected)");
      return;
    }
    auto &v = fields[state.selectedField];

    auto stateBefore = entry.resourceType->serialize();

    ImGui::TextDisabled("%s  Field", ICON_MDI_VARIABLE);
    ImGui::Separator();

    char nameBuf[128]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", v.name.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##rname", nameBuf, sizeof(nameBuf))) {
      v.name = nameBuf;
    }

    static const char* kindNames[] = {
      "Int", "Float", "Bool", "Vec3", "Quat",
      "Object Ref", "Prefab Ref", "Asset Ref",
    };
    int kindIdx = static_cast<int>(v.kind);
    ImGui::TextUnformatted("Type");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##rkind", &kindIdx, kindNames, IM_ARRAYSIZE(kindNames))) {
      v.kind = static_cast<Project::VarKind>(kindIdx);
      seedDefault(v);
    }

    if (v.kind == Project::VarKind::PREFAB_REF) {
      ImGui::TextUnformatted("Target Prefab");
      ImGui::SetNextItemWidth(-1);
      std::string label = "(none)";
      if (ctx.project) {
        auto *e = ctx.project->getAssets().getEntryByUUID(v.typeArg);
        if (e) label = e->name;
      }
      if (ImGui::BeginCombo("##rtarg", label.c_str())) {
        if (ctx.project) {
          for (const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::PREFAB)) {
            uint64_t entryUUID = e.getUUID();
            bool sel = (entryUUID == v.typeArg);
            std::string entryLabel = e.name + "##" + std::to_string(entryUUID);
            if (ImGui::Selectable(entryLabel.c_str(), sel)) v.typeArg = entryUUID;
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TextUnformatted("Default");
    ImGui::SetNextItemWidth(-1);
    switch (v.kind) {
      case Project::VarKind::INT: {
        int val = v.defaultValue.get<int32_t>();
        if (ImGui::DragInt("##rdef", &val)) v.defaultValue.set<int32_t>(val);
        break;
      }
      case Project::VarKind::FLOAT: {
        float val = v.defaultValue.get<float>();
        if (ImGui::DragFloat("##rdef", &val, 0.01f)) v.defaultValue.set<float>(val);
        break;
      }
      case Project::VarKind::BOOL: {
        bool val = v.defaultValue.get<bool>();
        if (ImGui::Checkbox("##rdef", &val)) v.defaultValue.set<bool>(val);
        break;
      }
      case Project::VarKind::VEC3: {
        glm::vec3 val = v.defaultValue.get<glm::vec3>();
        if (ImGui::DragFloat3("##rdef", &val.x, 0.01f)) v.defaultValue.set<glm::vec3>(val);
        break;
      }
      case Project::VarKind::QUAT: {
        glm::quat q = v.defaultValue.get<glm::quat>();
        float xyzw[4]{q.x, q.y, q.z, q.w};
        if (ImGui::DragFloat4("##rdef", xyzw, 0.01f)) {
          v.defaultValue.set<glm::quat>(glm::quat{xyzw[3], xyzw[0], xyzw[1], xyzw[2]});
        }
        break;
      }
      case Project::VarKind::OBJECT_REF: ImGui::TextDisabled("(null - set per instance)"); break;
      case Project::VarKind::PREFAB_REF: ImGui::TextDisabled("(null - set per instance)"); break;
      case Project::VarKind::ASSET_REF:  ImGui::TextDisabled("(asset ref - TODO)"); break;
    }

    if (stateBefore != entry.resourceType->serialize()) {
      Editor::UndoRedo::getHistory().markChanged("Edit Resource Field");
      persist(entry);
    }
  }
}

void Editor::ResourceTypeEditor::draw(Project::AssetManagerEntry &entry)
{
  if (entry.type != Project::FileType::RESOURCE_TYPE || !entry.resourceType) return;

  auto &state = states()[entry.resourceType->uuid];

  ImGui::Separator();
  ImGui::TextDisabled("Resource Type Schema");

  // Two-pane: field list on the left, details on the right. Inside the
  // AssetInspector window the list pane is narrow on purpose - the user
  // does most editing in the right-hand details pane.
  const float listW = ImGui::GetContentRegionAvail().x * 0.45f;
  if (ImGui::BeginChild("##rtypeList", ImVec2(listW, 240), true)) {
    drawFieldsPanel(entry, state);
  }
  ImGui::EndChild();

  ImGui::SameLine();
  if (ImGui::BeginChild("##rtypeDetails", ImVec2(0, 240), true)) {
    drawFieldDetails(entry, state);
  }
  ImGui::EndChild();
}
