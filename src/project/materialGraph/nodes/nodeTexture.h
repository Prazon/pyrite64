/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../context.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/assets/textureEditor.h"

namespace Project::MaterialGraph::Node
{
  // One of the two N64 RDP texture tiles. `slot` picks which tile (0 or 1)
  // this node populates. Plug two of these into Output for a multitexture
  // material; one for a normal single-tex material. Owns a MaterialTex and
  // hands it to TextureEditor::draw so the in-graph UX matches the model
  // editor's per-material texture inspector field-for-field, including the
  // placeholder dropdown used for runtime UV-scrolling scripts.
  class Texture : public Base
  {
    private:
      int slot{0};
      Project::Assets::MaterialTex tex{};

    public:
      constexpr static const char* NAME = ICON_MDI_IMAGE_OUTLINE " Texture";

      Texture()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(::Project::Graph::makeNodeStyle(
          ::Project::Graph::NodeCategory::MaterialConstant));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);

        // Reasonable defaults so a fresh node previews correctly.
        tex.repeat.value = glm::vec2{1.0f, 1.0f};
        tex.scale.value  = glm::ivec2{0, 0};
      }

      void draw() override
      {
        ImGui::SetNextItemWidth(80.0f);
        ImGui::Combo("Slot##s", &slot, "Tex 0\0Tex 1\0");

        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Placeholder##PH", &tex.dynType.value,
                     "None\0" "Tile\0" "Texture + Tile\0");

        if (!ctx.project) return;

        // FULL placeholder mode keeps the slot but lets a script swap the
        // underlying texture at runtime — there's no picker, only a size.
        if (tex.dynType.value == tex.DYN_TYPE_FULL) {
          int sz[2] = {tex.texSize.value[0], tex.texSize.value[1]};
          ImGui::SetNextItemWidth(140.0f);
          if (ImGui::InputInt2("Size", sz)) {
            tex.texSize.value = glm::ivec2{sz[0], sz[1]};
          }
        } else {
          // Wider-than-typical-node, but matches modelEditor and gives the
          // image preview enough room to be useful.
          ImGui::PushItemWidth(180.0f);
          ::Editor::TextureEditor::draw(tex);
          ImGui::PopItemWidth();
        }
      }

      void serialize(nlohmann::json &j) override
      {
        j["slot"] = slot;
        j["tex"]  = tex.serialize();
      }
      void deserialize(nlohmann::json &j) override
      {
        slot = j.value("slot", 0);
        if (j.contains("tex")) {
          tex.deserialize(j["tex"]);
        } else if (j.contains("texUUID")) {
          // Back-compat for graphs saved before this node owned a full
          // MaterialTex (only `texUUID` was persisted).
          tex.texUUID.value = j.value<uint64_t>("texUUID", 0);
          tex.set.value     = (tex.texUUID.value != 0);
        }
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        auto &dst = (slot == 0) ? out.tex0 : out.tex1;
        dst = tex;
        dst.set.value = (tex.texUUID.value != 0) || tex.dynType.value != tex.DYN_TYPE_NONE;
      }
  };
}
