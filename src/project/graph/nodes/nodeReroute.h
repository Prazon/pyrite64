/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Routing knot: a tiny pass-through node the user inserts on a wire
  // (double-click in the editor) to bend the cable around obstacles.
  // Exec-only for v1; data-pin reroutes can ship later if there's
  // demand. Codegen relies on the implicit jump(0) the build loop in
  // graph.cpp appends after every node, so build() itself is empty.
  class Reroute : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_DOTS_HORIZONTAL_CIRCLE " Reroute";

      Reroute()
      {
        uuid = Utils::Hash::randomU64();
        // Empty title hides the header bar on most ImGui themes; the
        // transparent style below collapses what little remains.
        setTitle("");

        auto ns = makeNodeStyle(NodeCategory::ExecBranch);
        ns->bg               = IM_COL32(0, 0, 0, 0);
        ns->header_bg        = IM_COL32(0, 0, 0, 0);
        ns->border_color     = IM_COL32(0, 0, 0, 0);
        ns->padding          = ImVec4(2.0f, 2.0f, 2.0f, 2.0f);
        ns->radius           = 4.0f;
        setStyle(std::move(ns));

        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      // Tiny spacer so the node has a non-zero footprint between its
      // input and output triangles. ImNodeFlow lays the IN and OUT
      // pins on opposite sides of whatever this draws.
      void draw() override {
        ImGui::Dummy(ImVec2(8.0f, 8.0f));
      }

      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      // Pass-through: the build loop in graph.cpp auto-appends jump(0)
      // when the node has any outgoing link, which is exactly the
      // routing semantic we want.
      void build(BuildCtx &) override {}
  };
}
