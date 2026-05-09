/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assetInspector.h"
#include "imgui.h"
#include "../editorScene.h"
#include "assets/resourceTypeEditor.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../../context.h"
#include "../../../utils/fs.h"
#include "../../../utils/string.h"
#include "../../../utils/textureFormats.h"

using FileType = Project::FileType;

int Selecteditem  = 0;

namespace
{
  uint32_t countChildBones(const T3DM::Bone &node) {
    uint32_t count = node.children.size();
    for (const auto &child : node.children) {
      count += countChildBones(*child);
    }
    return count;
  };
}

Editor::AssetInspector::AssetInspector() {
}

void Editor::AssetInspector::draw() {
  if (ctx.selAssetUUID == 0) {
    ImGui::Text("No Asset selected");
    return;
  }

  auto asset = ctx.project->getAssets().getEntryByUUID(ctx.selAssetUUID);
  if (!asset) {
    ctx.selAssetUUID = 0;
    return;
  }

  bool hasAssetConf = true;
  if (asset->type == FileType::CODE_OBJ
    || asset->type == FileType::CODE_GLOBAL
    || asset->type == FileType::PREFAB
    || asset->type == FileType::RESOURCE_TYPE
    || asset->type == FileType::RESOURCE_INSTANCE)
  {
    hasAssetConf = false;
  }

  ImGui::Text("File: %s", asset->name.c_str());
  if (hasAssetConf && ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
  {
    auto confBefore = asset->conf.serialize();

    ImTable::start("Settings");

    if (asset->type == FileType::IMAGE)
    {
      ImTable::addComboBox("Format", asset->conf.format, Utils::TEX_TYPES, Utils::TEX_TYPE_COUNT);
    }
    else if (asset->type == FileType::MODEL_3D)
    {
      if (ImTable::add("Base-Scale", asset->conf.baseScale)) {
        ctx.project->getAssets().reloadAssetByUUID(asset->getUUID());
      }
      ImTable::addCheckBox("Create BVH", asset->conf.gltfBVH);
    } else if (asset->type == FileType::FONT)
    {
      ImTable::add("Size", asset->conf.baseScale);
      ImTable::addProp("ID", asset->conf.fontId);

      ImTable::add("Charset");
      ImGui::InputTextMultiline("##", &asset->conf.fontCharset.value);
    }
    else if (asset->type == FileType::AUDIO)
    {
      ImTable::addProp("Force-Mono", asset->conf.wavForceMono);

      //ImTable::addProp("Sample-Rate", asset->conf.wavResampleRate);
      ImTable::addVecComboBox<ImTable::ComboEntry>("Sample-Rate", {
          { 0, "Original" },
          { 8000, "8000 Hz" },
          { 11025, "11025 Hz" },
          { 16000, "16000 Hz" },
          { 22050, "22050 Hz" },
          { 32000, "32000 Hz" },
          { 44100, "44100 Hz" },
        }, asset->conf.wavResampleRate.value
      );

      ImTable::addVecComboBox<ImTable::ComboEntry>("Compression", {
          { 0, "None" },
          { 1, "VADPCM" },
          { 3, "Opus" },
        }, asset->conf.wavCompression.value
      );
    }

    if (asset->type != FileType::AUDIO && asset->type != FileType::MUSIC_XM)
    {
      ImTable::addComboBox("Compression", (int&)asset->conf.compression, {
        "Project Default", "None",
        "Level 1 - Fast",
        "Level 2 - Good",
        "Level 3 - High",
      });
    }

    ImTable::addCheckBox("Exclude", asset->conf.exclude);

    ImTable::end();

    if (confBefore != asset->conf.serialize()) {
      ctx.project->getAssets().markAssetMetaDirty(asset->getUUID());
    }
  }

  if (asset->type == FileType::RESOURCE_TYPE && asset->resourceType)
  {
    // Editor-authored .p64restype: surface the field-schema editor so the
    // user can add/rename/delete fields without leaving the AssetInspector.
    Editor::ResourceTypeEditor::draw(*asset);
  }

  if (asset->type == FileType::RESOURCE_INSTANCE && asset->resource)
  {
    auto &assetMgr = ctx.project->getAssets();
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
      } else {
        auto stateBefore = asset->resource->serialize();

        ImGui::Separator();
        ImTable::start("ResourceFields");
        for (const auto &def : fields) {
          // Resolve effective value: per-instance override falls back to the
          // type's default. Mirrors the prefab override pattern from
          // objectInspector.cpp.
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
              // Refs surface as a uint64 picker. Asset refs let the user
              // pick any asset by uuid; prefab refs filter to PREFABs;
              // object refs are not meaningful at instance-level so are
              // shown read-only.
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
                  // ASSET_REF: list everything; no per-kind filter yet.
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

  if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (asset->type == FileType::IMAGE && asset->texture) {
      auto imgSize = asset->texture->getSize();

      float maxWidth = ImGui::GetContentRegionAvail().x - 8_px;
      if(maxWidth > 256_px)maxWidth = 256_px;
      float imgRatio = imgSize.x / imgSize.y;
      imgSize.x = maxWidth;
      imgSize.y = maxWidth / imgRatio;

      ImGui::Image(ImTextureRef(asset->texture->getGPUTex()), imgSize);
      ImGui::Text("%dx%dpx", asset->texture->getWidth(), asset->texture->getHeight());
    }
    if (asset->type == FileType::MODEL_3D) {
      uint32_t triCount = 0;
      for (auto &model : asset->model.t3dm.models) {
        triCount += model.triangles.size();
      }

      uint32_t boneCount = 0;
      for(auto &skel : asset->model.t3dm.skeletons) {
        boneCount += countChildBones(skel);
      }

      ImGui::BeginTable("ModelInfo", 2);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
        ImGui::Text("Meshes");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", static_cast<int>(asset->model.t3dm.models.size()));

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
        ImGui::Text("Triangles");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", triCount);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
        ImGui::Text("Bones");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", boneCount);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
        ImGui::Text("Animations");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", static_cast<int>(asset->model.t3dm.animations.size()));

      ImGui::EndTable();
    }
  }
}
