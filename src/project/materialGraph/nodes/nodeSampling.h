/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::MaterialGraph::Node
{
  // Texture-sampling pipeline knobs that aren't per-tile: perspective-
  // correct UVs, RDP dither mode, and texture filter (nearest / bilinear).
  // Sinks into the Output "Sampling" pin slot. Mirrors the "Sampling"
  // subsection in modelEditor.cpp.
  class Sampling : public Base
  {
    private:
      bool setPersp{false};  bool persp{true};
      bool setDither{false}; int  dither{15};
      bool setFilter{false}; int  filter{0};

    public:
      constexpr static const char* NAME = ICON_MDI_TUNE " Sampling";

      Sampling()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(::Project::Graph::makeNodeStyle(
          ::Project::Graph::NodeCategory::MaterialConstant));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);
      }

      void draw() override
      {
        ImGui::PushItemWidth(160.0f);

        ImGui::Checkbox("Perspective##s", &setPersp);
        if (setPersp) ImGui::Checkbox("##P", &persp);

        ImGui::Checkbox("Dither##s", &setDither);
        if (setDither) {
          // Same 16-entry list modelEditor.cpp uses; combo string lives
          // inline here to avoid a cross-TU constant.
          ImGui::Combo("##D", &dither,
            "Square / Square\0"
            "Square / Inv. Square\0"
            "Square / Noise\0"
            "Square / None\0"
            "Bayer / Bayer\0"
            "Bayer / Inv. Bayer\0"
            "Bayer / Noise\0"
            "Bayer / None\0"
            "Noise / Square\0"
            "Noise / Inv. Square\0"
            "Noise / Noise\0"
            "Noise / None\0"
            "None / Bayer\0"
            "None / Inv. Bayer\0"
            "None / Noise\0"
            "None / None\0");
        }

        ImGui::Checkbox("Filtering##s", &setFilter);
        if (setFilter) {
          int val = (filter == 0) ? 0 : 1;
          if (ImGui::Combo("##FL", &val, "Nearest\0Bilinear\0")) {
            filter = (val == 0) ? 0 : 2;
          }
        }

        ImGui::PopItemWidth();
      }

      void serialize(nlohmann::json &j) override
      {
        j["setP"]  = setPersp;  j["p"]  = persp;
        j["setD"]  = setDither; j["d"]  = dither;
        j["setFL"] = setFilter; j["fl"] = filter;
      }
      void deserialize(nlohmann::json &j) override
      {
        setPersp  = j.value("setP",  false); persp  = j.value("p", true);
        setDither = j.value("setD",  false); dither = j.value("d", 15);
        setFilter = j.value("setFL", false); filter = j.value("fl", 0);
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        if (setPersp)  { out.persp.value  = persp;  out.perspSet.value  = true; }
        if (setDither) { out.dither.value = dither; out.ditherSet.value = true; }
        if (setFilter) { out.filter.value = filter; out.filterSet.value = true; }
      }
  };
}
