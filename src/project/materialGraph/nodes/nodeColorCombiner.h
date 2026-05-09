/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../n64/ccMapping.h"

#include "imgui_internal.h"

namespace Project::MaterialGraph::Node
{
  // Owns the Color Combiner state — the single most distinctive piece of
  // an N64 material. Stores the packed cc value plus a 2-cycle flag; the
  // inspector unpacks/repacks via N64::CC helpers identical to the inline
  // model-editor view, so the user gets the same UI in node form.
  class ColorCombiner : public Base
  {
    private:
      uint64_t cc{0};

    public:
      constexpr static const char* NAME = ICON_MDI_PALETTE " Color Combiner";

      ColorCombiner()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(std::make_shared<ImFlow::NodeStyle>(
          IM_COL32(190, 130, 230, 255), ImColor(0, 0, 0, 255), 4.0f));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);
      }

      void draw() override
      {
        auto usage = N64::CC::getUsage(cc);
        glm::ivec4 cc0[2], cca[2];
        N64::CC::unpackCC(cc, cc0[0], cca[0], cc0[1], cca[1]);

        ImGui::PushItemWidth(110.0f);

        bool twoCycle = usage.twoCycle;
        if (ImGui::Checkbox("2-Cycle", &twoCycle) && twoCycle) {
          cc0[1][0] = N64::CC::NAMES_COL_A.size() - 1;
          cc0[1][1] = N64::CC::NAMES_COL_B.size() - 1;
          cc0[1][2] = N64::CC::NAMES_COL_C.size() - 1;
          cc0[1][3] = 0;
          cca[1][0] = N64::CC::NAMES_ALPHA_A.size() - 1;
          cca[1][1] = N64::CC::NAMES_ALPHA_B.size() - 1;
          cca[1][2] = N64::CC::NAMES_ALPHA_C.size() - 1;
          cca[1][3] = 0;
        }
        usage.twoCycle = twoCycle;

        for (int c = 0; c < (usage.twoCycle ? 2 : 1); ++c) {
          ImGui::PushID(c);
          ImGui::TextDisabled("Cycle %d", c);
          ImGui::Combo("##C_A",  &cc0[c][0], N64::CC::NAMES_COL_A.data(), N64::CC::NAMES_COL_A.size());
          ImGui::Combo("##C_B",  &cc0[c][1], N64::CC::NAMES_COL_B.data(), N64::CC::NAMES_COL_B.size());
          ImGui::Combo("##C_C",  &cc0[c][2], N64::CC::NAMES_COL_C.data(), N64::CC::NAMES_COL_C.size());
          ImGui::Combo("##C_D",  &cc0[c][3], N64::CC::NAMES_COL_D.data(), N64::CC::NAMES_COL_D.size());
          ImGui::Combo("##A_A", &cca[c][0], N64::CC::NAMES_ALPHA_A.data(), N64::CC::NAMES_ALPHA_A.size());
          ImGui::Combo("##A_B", &cca[c][1], N64::CC::NAMES_ALPHA_B.data(), N64::CC::NAMES_ALPHA_B.size());
          ImGui::Combo("##A_C", &cca[c][2], N64::CC::NAMES_ALPHA_C.data(), N64::CC::NAMES_ALPHA_C.size());
          ImGui::Combo("##A_D", &cca[c][3], N64::CC::NAMES_ALPHA_D.data(), N64::CC::NAMES_ALPHA_D.size());
          ImGui::PopID();
        }

        if (!usage.twoCycle) { cc0[1] = cc0[0]; cca[1] = cca[0]; }
        cc = N64::CC::packCC(cc0[0], cca[0], cc0[1], cca[1]);
        if (usage.twoCycle) cc |= 0x0000000000000001ULL; // RDPQ_COMBINER_2PASS marker bit

        ImGui::PopItemWidth();
      }

      void serialize(nlohmann::json &j) override { j["cc"] = cc; }
      void deserialize(nlohmann::json &j) override { cc = j.value<uint64_t>("cc", 0); }

      void contribute(::Project::Assets::Material &out) const override
      {
        out.cc.value = cc;
        out.ccSet.value = true;
      }
  };
}
