/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::MaterialGraph::Node
{
  // CC register-value providers: PRIM color, ENV color, primLOD frac, K4/K5
  // constants. The N64 RDP exposes these as inputs the Color Combiner can
  // sample; this node owns all four with per-channel "set" toggles. Mirrors
  // the "Values" subsection in modelEditor.cpp.
  //
  // The C++ symbol stays `Colors` (it's the persisted type-table key — see
  // TYPE_COLORS in graph.cpp); only the display name changed.
  class Colors : public Base
  {
    private:
      bool      setPrim{false};
      glm::vec4 prim{0.0f, 0.0f, 0.0f, 1.0f};
      bool      setEnv{false};
      glm::vec4 env{0.5f, 0.5f, 0.5f, 1.0f};

      bool      setLod{false};
      uint32_t  primLod{0};

      bool      setK4K5{false};
      glm::ivec2 k4k5{0, 0};

    public:
      constexpr static const char* NAME = ICON_MDI_PALETTE_OUTLINE " Values";

      Colors()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(::Project::Graph::makeNodeStyle(
          ::Project::Graph::NodeCategory::MaterialConstant));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);
      }

      void draw() override
      {
        ImGui::Checkbox("Prim##setP", &setPrim);
        if (setPrim) {
          ImGui::SetNextItemWidth(160.0f);
          ImGui::ColorEdit4("##P", &prim.x, ImGuiColorEditFlags_AlphaBar);
        }
        ImGui::Checkbox("Env##setE", &setEnv);
        if (setEnv) {
          ImGui::SetNextItemWidth(160.0f);
          ImGui::ColorEdit4("##E", &env.x, ImGuiColorEditFlags_AlphaBar);
        }
        ImGui::Checkbox("LOD##setL", &setLod);
        if (setLod) {
          int v = static_cast<int>(primLod);
          ImGui::SetNextItemWidth(160.0f);
          if (ImGui::SliderInt("##L", &v, 0, 255)) primLod = static_cast<uint32_t>(v);
        }
        ImGui::Checkbox("K4/K5##setK", &setK4K5);
        if (setK4K5) {
          ImGui::SetNextItemWidth(160.0f);
          ImGui::SliderInt2("##K", &k4k5.x, 0, 255);
        }
      }

      void serialize(nlohmann::json &j) override
      {
        j["setPrim"]  = setPrim;
        j["prim"]     = {prim.x, prim.y, prim.z, prim.w};
        j["setEnv"]   = setEnv;
        j["env"]      = {env.x, env.y, env.z, env.w};
        j["setLod"]   = setLod;
        j["lod"]      = primLod;
        j["setK4K5"]  = setK4K5;
        j["k4k5"]     = {k4k5.x, k4k5.y};
      }
      void deserialize(nlohmann::json &j) override
      {
        setPrim = j.value("setPrim", false);
        if (j.contains("prim")) prim = {j["prim"][0], j["prim"][1], j["prim"][2], j["prim"][3]};
        setEnv = j.value("setEnv", false);
        if (j.contains("env")) env = {j["env"][0], j["env"][1], j["env"][2], j["env"][3]};
        setLod  = j.value("setLod", false);
        primLod = j.value<uint32_t>("lod", 0u);
        setK4K5 = j.value("setK4K5", false);
        if (j.contains("k4k5")) k4k5 = {j["k4k5"][0], j["k4k5"][1]};
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        if (setPrim) {
          out.primColor.value = prim;
          out.primColorSet.value = true;
        }
        if (setEnv) {
          out.envColor.value = env;
          out.envColorSet.value = true;
        }
        if (setLod) {
          out.primLod.value = primLod;
          out.primLodSet.value = true;
        }
        if (setK4K5) {
          out.k4k5.value = k4k5;
          out.k4k5Set.value = true;
        }
      }
  };
}
