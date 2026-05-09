/**
* Per-asset RESOURCE_INSTANCE editor. Mirrors the field editor that
* previously lived inline in AssetInspector for RESOURCE_INSTANCE assets.
*/
#include "resourceInstanceEditor.h"

#include "assetEditorDocking.h"
#include "../assetInspector.h"
#include "../../../imgui/helper.h"
#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{640, 480};
}

void Editor::ResourceInstanceEditor::drawInstancePane()
{
  auto &assetMgr = ctx.project->getAssets();
  auto asset = assetMgr.getEntryByUUID(assetUUID);
  if (!asset || !asset->resource) {
    ImGui::TextDisabled("(resource instance body missing)");
    return;
  }

  auto *typeEntry = assetMgr.getEntryByUUID(asset->resource->typeUuid);

  ImTable::start("Resource");
  ImTable::add("Type");
  if (typeEntry) {
    ImGui::TextUnformatted(typeEntry->name.c_str());
  } else {
    ImGui::TextDisabled("(unresolved typeUuid 0x%016llX)",
      (unsigned long long)asset->resource->typeUuid);
  }
  ImTable::end();

  const bool editorAuthored = (typeEntry && typeEntry->resourceType);

  if (editorAuthored)
  {
    // Editor-authored schema: full 8-kind ladder, uuid-keyed storage.
    const auto &fields = typeEntry->resourceType->fields;
    if (fields.empty()) {
      ImGui::TextDisabled("(this resource type defines no fields)");
      return;
    }

    auto stateBefore = asset->resource->serialize();

    ImGui::Separator();
    ImTable::start("ResourceFields");
    for (const auto &def : fields) {
      // Per-instance value falls back to the type's default. Mirrors the
      // prefab override pattern used in objectInspector.cpp.
      auto it = asset->resource->uuidValues.find(def.uuid);
      GenericValue effective = (it != asset->resource->uuidValues.end())
        ? it->second : def.defaultValue;

      ImTable::add(def.name);
      ImGui::PushID(static_cast<int>(def.uuid));
      ImGui::SetNextItemWidth(-1);

      bool changed = false;
      switch (def.kind) {
        case Project::VarKind::INT: {
          int val = effective.get<int32_t>();
          if (ImGui::DragInt("##v", &val)) {
            effective.set<int32_t>(val); changed = true;
          }
          break;
        }
        case Project::VarKind::FLOAT: {
          float val = effective.get<float>();
          if (ImGui::DragFloat("##v", &val, 0.01f)) {
            effective.set<float>(val); changed = true;
          }
          break;
        }
        case Project::VarKind::BOOL: {
          bool val = effective.get<bool>();
          if (ImGui::Checkbox("##v", &val)) {
            effective.set<bool>(val); changed = true;
          }
          break;
        }
        case Project::VarKind::VEC3: {
          glm::vec3 val = effective.get<glm::vec3>();
          if (ImGui::DragFloat3("##v", &val.x, 0.01f)) {
            effective.set<glm::vec3>(val); changed = true;
          }
          break;
        }
        case Project::VarKind::QUAT: {
          glm::quat q = effective.get<glm::quat>();
          float xyzw[4]{q.x, q.y, q.z, q.w};
          if (ImGui::DragFloat4("##v", xyzw, 0.01f)) {
            effective.set<glm::quat>(glm::quat{xyzw[3], xyzw[0], xyzw[1], xyzw[2]});
            changed = true;
          }
          break;
        }
        case Project::VarKind::OBJECT_REF:
        case Project::VarKind::PREFAB_REF:
        case Project::VarKind::ASSET_REF: {
          uint64_t val = effective.get<uint64_t>();
          std::string label = "(none)";
          if (val != 0) {
            auto *e = assetMgr.getEntryByUUID(val);
            if (e) label = e->name;
            else label = "0x" + std::to_string(val);
          }
          if (def.kind == Project::VarKind::OBJECT_REF) {
            ImGui::TextDisabled("%s", label.c_str());
          } else if (ImGui::BeginCombo("##v", label.c_str())) {
            if (ImGui::Selectable("(none)", val == 0)) {
              effective.set<uint64_t>(0); changed = true;
            }
            if (def.kind == Project::VarKind::PREFAB_REF) {
              for (const auto &e : assetMgr.getTypeEntries(Project::FileType::PREFAB)) {
                bool sel = (e.getUUID() == val);
                std::string entryLabel = e.name + "##" + std::to_string(e.getUUID());
                if (ImGui::Selectable(entryLabel.c_str(), sel)) {
                  effective.set<uint64_t>(e.getUUID()); changed = true;
                }
              }
            } else {
              for (const auto &typed : assetMgr.getEntries()) {
                for (const auto &e : typed) {
                  bool sel = (e.getUUID() == val);
                  std::string entryLabel = e.name + "##" + std::to_string(e.getUUID());
                  if (ImGui::Selectable(entryLabel.c_str(), sel)) {
                    effective.set<uint64_t>(e.getUUID()); changed = true;
                  }
                }
              }
            }
            ImGui::EndCombo();
          }
          break;
        }
      }

      if (changed) {
        asset->resource->uuidValues[def.uuid] = std::move(effective);
      }
      ImGui::PopID();
    }
    ImTable::end();

    if (stateBefore != asset->resource->serialize()) {
      Utils::FS::saveTextFile(asset->path, asset->resource->serialize());
    }
  }
  else if (typeEntry && !typeEntry->params.fields.empty())
  {
    // Header-authored schema: legacy string-keyed values, limited type set.
    auto stateBefore = asset->resource->serialize();

    ImGui::Separator();
    ImTable::start("ResourceFields");
    for (const auto &field : typeEntry->params.fields) {
      // Only fields with a P64::Name attribute are user-editable; the rest
      // are runtime-internal scratch fields that get zero-init at load.
      auto metaName = field.attr.find("P64::Name");
      if (metaName == field.attr.end()) continue;
      const std::string &label = metaName->second;

      auto &slot = asset->resource->values[field.name];
      if (slot.empty()) slot = field.defaultValue;
      if (slot.empty()) slot = "0";

      ImTable::add(label);
      ImGui::PushID(field.name.c_str());
      switch (field.type) {
        case Utils::DataType::u8: case Utils::DataType::u16: case Utils::DataType::u32:
        case Utils::DataType::s8: case Utils::DataType::s16: case Utils::DataType::s32: {
          int v = 0;
          try { v = std::stoi(slot); } catch (...) {}
          if (ImGui::InputInt("##v", &v)) slot = std::to_string(v);
          break;
        }
        case Utils::DataType::f32: {
          float v = 0.f;
          try { v = std::stof(slot); } catch (...) {}
          if (ImGui::InputFloat("##v", &v)) slot = std::to_string(v);
          break;
        }
        case Utils::DataType::string:
          ImGui::InputText("##v", &slot);
          break;
        default:
          ImGui::TextDisabled("(unsupported field type)");
          break;
      }
      ImGui::PopID();
    }
    ImTable::end();

    if (stateBefore != asset->resource->serialize()) {
      Utils::FS::saveTextFile(asset->path, asset->resource->serialize());
    }
  } else if (!typeEntry) {
    ImGui::TextDisabled("Resource type missing - open the .p64res in a text editor to repair.");
  } else {
    ImGui::TextDisabled("(this resource type defines no editable fields)");
  }
}

bool Editor::ResourceInstanceEditor::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto asset = assetManager.getEntryByUUID(assetUUID);
  if (!asset) return false;
  if (asset->type != Project::FileType::RESOURCE_INSTANCE) return false;

  winName = "Resource: " + asset->name
    + "###ResourceInstanceEditorWin_" + std::to_string(assetUUID);

  Editor::setupAssetEditorDocking(defDockId, firstDockFrame);

  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {
      mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
      mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
    },
    ImGuiCond_FirstUseEver
  );

  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen);

  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterW  = 6_px;
  float minRightW  = 220_px;
  float minLeftW   = 240_px;
  float rightW     = std::clamp(fullAvail.x * splitFrac, minRightW, std::max(minRightW, fullAvail.x - minLeftW - splitterW));
  float leftW      = std::max(minLeftW, fullAvail.x - splitterW - rightW);

  ImGui::BeginChild("##resinstMain", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
  drawInstancePane();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::InvisibleButton("##resinstSplit", ImVec2(splitterW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (fullAvail.x > splitterW * 2) {
      splitFrac -= dx / (fullAvail.x - splitterW);
      splitFrac = std::clamp(splitFrac, 0.20f, 0.55f);
    }
  } else {
    splitDragging = false;
  }
  if (ImGui::IsItemHovered() || splitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(splitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {(a.x + b.x) * 0.5f - 1.0f, a.y},
      {(a.x + b.x) * 0.5f + 1.0f, b.y},
      col
    );
  }

  ImGui::SameLine();
  ImGui::BeginChild("##resinstInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
  Editor::AssetInspector::draw(assetUUID);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::ResourceInstanceEditor::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
