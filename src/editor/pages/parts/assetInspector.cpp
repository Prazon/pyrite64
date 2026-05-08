/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assetInspector.h"
#include "imgui.h"
#include "../editorScene.h"
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

    if (typeEntry && !typeEntry->params.fields.empty())
    {
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
      ImGui::TextDisabled("Resource type header missing — open the .p64res in a text editor to repair.");
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
