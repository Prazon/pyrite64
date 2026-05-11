/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::MaterialGraph::Node
{
  // Prim/Env color register values. The N64 RDP exposes two register
  // colors that the Color Combiner can sample (PRIM, ENV); this node
  // owns both with per-channel sets so the user can plug just one.
  class Colors : public Base
  {
    private:
      bool      setPrim{false};
      glm::vec4 prim{0.0f, 0.0f, 0.0f, 1.0f};
      bool      setEnv{false};
      glm::vec4 env{0.5f, 0.5f, 0.5f, 1.0f};

    public:
      constexpr static const char* NAME = ICON_MDI_PALETTE_OUTLINE " Colors";

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
      }

      void serialize(nlohmann::json &j) override
      {
        j["setPrim"] = setPrim;
        j["prim"] = {prim.x, prim.y, prim.z, prim.w};
        j["setEnv"] = setEnv;
        j["env"] = {env.x, env.y, env.z, env.w};
      }
      void deserialize(nlohmann::json &j) override
      {
        setPrim = j.value("setPrim", false);
        if (j.contains("prim")) prim = {j["prim"][0], j["prim"][1], j["prim"][2], j["prim"][3]};
        setEnv = j.value("setEnv", false);
        if (j.contains("env")) env = {j["env"][0], j["env"][1], j["env"][2], j["env"][3]};
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
      }
  };
}
