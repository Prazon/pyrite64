/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../n64/libdragon.h"

namespace Project::MaterialGraph::Node
{
  // Render-mode bits: depth, blender, AA, alpha-clip, fog, fixed-Z. Each
  // flag has a separate "set" toggle so a material can leave any field at
  // the engine default by simply not enabling its toggle. Mirrors the
  // "Render Modes" subsection in modelEditor.cpp.
  class RenderMode : public Base
  {
    private:
      bool     setZmode{false};   int zmode{3};
      bool     setBlender{false}; uint32_t blender{0};
      bool     setAA{false};      int aa{0};
      bool     setAlpha{false};   int alpha{0};
      bool     setFog{false};     uint32_t fog{0};
      bool     setZprim{false};   int zprim{0}; int zdelta{0};

    public:
      constexpr static const char* NAME = ICON_MDI_LAYERS_TRIPLE_OUTLINE " Render Mode";

      RenderMode()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(::Project::Graph::makeNodeStyle(
          ::Project::Graph::NodeCategory::MaterialConstant));
        addOUT<TypeMatProp>("", PIN_STYLE_MATPROP);
      }

      void draw() override
      {
        ImGui::PushItemWidth(140.0f);

        ImGui::Checkbox("Depth##s", &setZmode);
        if (setZmode) ImGui::Combo("##Z", &zmode, "None\0Read\0Write\0Read+Write\0");

        ImGui::Checkbox("Anti-Alias##s", &setAA);
        if (setAA) ImGui::Combo("##AA", &aa, "None\0Standard\0Reduced\0");

        ImGui::Checkbox("Blender##s", &setBlender);
        if (setBlender) {
          int sel = blender == 0 ? 0 : (blender == 1 ? 1 : 2);
          if (ImGui::Combo("##B", &sel, "Opaque\0Multiply\0Additive\0")) {
            blender = (sel == 0) ? 0u : (sel == 1 ? 1u : 2u);
          }
        }

        ImGui::Checkbox("Alpha-Clip##s", &setAlpha);
        if (setAlpha) ImGui::SliderInt("##A", &alpha, 0, 255);

        ImGui::Checkbox("Fog##s", &setFog);
        if (setFog) {
          int sel = (fog == 0) ? 0 : 1;
          if (ImGui::Combo("##F", &sel, "None\0Standard\0")) {
            fog = (sel == 0) ? 0u : static_cast<uint32_t>(RDPQ_FOG_STANDARD);
          }
        }

        ImGui::Checkbox("Fixed-Z##s", &setZprim);
        if (setZprim) {
          ImGui::SetNextItemWidth(70.0f); ImGui::InputInt("##ZP", &zprim, 0);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(70.0f); ImGui::InputInt("##ZD", &zdelta, 0);
        }

        ImGui::PopItemWidth();
      }

      void serialize(nlohmann::json &j) override
      {
        j["setZ"]  = setZmode;     j["z"]  = zmode;
        j["setB"]  = setBlender;   j["b"]  = blender;
        j["setAA"] = setAA;        j["aa"] = aa;
        j["setA"]  = setAlpha;     j["a"]  = alpha;
        j["setF"]  = setFog;       j["f"]  = fog;
        j["setZP"] = setZprim;     j["zp"] = zprim;   j["zd"] = zdelta;
      }
      void deserialize(nlohmann::json &j) override
      {
        setZmode   = j.value("setZ",  false); zmode   = j.value("z", 3);
        setBlender = j.value("setB",  false); blender = j.value<uint32_t>("b", 0u);
        setAA      = j.value("setAA", false); aa      = j.value("aa", 0);
        setAlpha   = j.value("setA",  false); alpha   = j.value("a", 0);
        setFog     = j.value("setF",  false); fog     = j.value<uint32_t>("f", 0u);
        setZprim   = j.value("setZP", false); zprim   = j.value("zp", 0);
                                              zdelta  = j.value("zd", 0);
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        if (setZmode)   { out.zmode.value     = zmode;     out.zmodeSet.value     = true; }
        if (setBlender) { out.blender.value   = blender;   out.blenderSet.value   = true; }
        if (setAA)      { out.aa.value        = aa;        out.aaSet.value        = true; }
        if (setAlpha)   { out.alphaComp.value = alpha;     out.alphaCompSet.value = true; }
        if (setFog)     { out.fog.value       = fog;       out.fogSet.value       = true; }
        if (setZprim)   { out.zprim.value     = zprim;     out.zprimSet.value     = true;
                          out.zdelta.value    = zdelta; }
      }
  };
}
