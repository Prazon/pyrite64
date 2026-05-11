/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::MaterialGraph::Node
{
  // T3D-side flags: backface culling, unlit toggle, fog-to-alpha, vertex
  // FX. None of these have engine defaults this node needs to "set" —
  // every flag is always written when this node is connected, since they
  // collectively define a material's geometry-level behavior.
  class Geometry : public Base
  {
    private:
      int      vertexFX{0};
      uint32_t drawFlags{0};
      bool     fogToAlpha{false};

    public:
      constexpr static const char* NAME = ICON_MDI_VECTOR_TRIANGLE " Geometry";

      Geometry()
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
        ImGui::Combo("Vertex FX##",
          &vertexFX,
          "None\0Spherical UV\0Cel-shade Color\0Cel-shade Alpha\0Outline\0UV Offset\0");

        // T3D::FLAG_NO_LIGHT etc. are simple bit flags; CheckboxFlags works
        // directly on the uint without an intermediate temporary.
        ImGui::CheckboxFlags("Unlit",       &drawFlags, 0x01);  // T3D::FLAG_NO_LIGHT
        ImGui::CheckboxFlags("Cull-Front",  &drawFlags, 0x04);  // T3D::FLAG_CULL_FRONT
        ImGui::CheckboxFlags("Cull-Back",   &drawFlags, 0x08);  // T3D::FLAG_CULL_BACK
        ImGui::Checkbox("Fog → Alpha", &fogToAlpha);
        ImGui::PopItemWidth();
      }

      void serialize(nlohmann::json &j) override
      {
        j["vfx"] = vertexFX;
        j["flags"] = drawFlags;
        j["f2a"] = fogToAlpha;
      }
      void deserialize(nlohmann::json &j) override
      {
        vertexFX = j.value("vfx", 0);
        drawFlags = j.value<uint32_t>("flags", 0u);
        fogToAlpha = j.value("f2a", false);
      }

      void contribute(::Project::Assets::Material &out) const override
      {
        out.vertexFX.value = vertexFX;
        out.drawFlags.value = drawFlags;
        out.fogToAlpha.value = fogToAlpha ? 1 : 0;
      }
  };
}
