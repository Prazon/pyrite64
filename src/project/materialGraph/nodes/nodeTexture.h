/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../context.h"
#include "../../assetManager.h"

namespace Project::MaterialGraph::Node
{
  // One of the two N64 RDP texture tiles. The `slot` field selects which
  // tile (0 or 1) this node populates. Plug two of these into the output
  // for a multitexture material; one for a normal single-tex material.
  class Texture : public Base
  {
    private:
      int      slot{0};
      uint64_t texUUID{0};

    public:
      constexpr static const char* NAME = ICON_MDI_IMAGE_OUTLINE " Texture";

      Texture()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(std::make_shared<ImFlow::NodeStyle>(
          IM_COL32(80, 180, 130, 255), ImColor(0, 0, 0, 255), 4.0f));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);
      }

      void draw() override
      {
        ImGui::SetNextItemWidth(80.0f);
        ImGui::Combo("Slot##s", &slot, "Tex 0\0Tex 1\0");

        // Image picker. Shows the currently-bound asset name and accepts
        // an ASSET drag-drop to retarget. Doesn't pull on the AssetManager
        // when ctx.project is null (shouldn't be — this node only renders
        // inside the editor — but be defensive about boot ordering).
        const char* curName = "(none)";
        if (ctx.project && texUUID != 0) {
          if (auto *e = ctx.project->getAssets().getEntryByUUID(texUUID)) {
            if (e->type == ::Project::FileType::IMAGE) curName = e->name.c_str();
          }
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Text("%s", curName);
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("ASSET")) {
            uint64_t dropped = *static_cast<const uint64_t*>(p->Data);
            if (ctx.project) {
              if (auto *e = ctx.project->getAssets().getEntryByUUID(dropped)) {
                if (e->type == ::Project::FileType::IMAGE) texUUID = dropped;
              }
            }
          }
          ImGui::EndDragDropTarget();
        }
      }

      void serialize(nlohmann::json &j) override
      {
        j["slot"] = slot;
        j["texUUID"] = texUUID;
      }
      void deserialize(nlohmann::json &j) override
      {
        slot = j.value("slot", 0);
        texUUID = j.value<uint64_t>("texUUID", 0);
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        auto &tex = (slot == 0) ? out.tex0 : out.tex1;
        tex.set.value = (texUUID != 0);
        tex.texUUID.value = texUUID;
        // Sensible defaults for a fresh slot — the user can override these
        // at the per-instance level via MatInstanceEditor on each model.
        if (tex.repeat.value.x == 0.0f) tex.repeat.value = glm::vec2{1.0f, 1.0f};
      }
  };
}
