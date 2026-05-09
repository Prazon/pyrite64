/**
* @copyright 2026 - Prazon
* @license MIT
*
* Material-graph: a parallel ImNodeFlow graph dedicated to .p64mat assets.
* Deliberately separate from Project::Graph (event/script graphs) — the
* topology is dataflow not control-flow, the pin types are typed material
* values not Logic/Value, and the node table is its own.
*
* Compile is "transcribe-only": each provider node owns a piece of the
* Material struct (CC, render-mode, prim/env color, texture slots, etc.)
* and contributes its fields to the output Material when reachable from
* the MaterialOutput sink. No expression composition.
*/
#pragma once

#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "ImNodeFlow.h"
#pragma GCC diagnostic pop

#include "../assets/material.h"
#include "nodes/baseNode.h"

namespace Project::MaterialGraph
{
  class Graph
  {
    public:
      ImFlow::ImNodeFlow graph{};

      static const std::vector<std::string>& getNodeNames();
      std::shared_ptr<Node::Base> addNode(uint32_t type, const ImVec2& pos);

      bool deserialize(const std::string &jsonData);
      std::string serialize();

      // Walk dataflow back from the MaterialOutput sink and overlay every
      // reachable provider node's fields onto `out`. `out` starts as a
      // default-constructed Material; provider nodes only touch the fields
      // they own, so the order in which they're visited doesn't matter for
      // the v1 (transcribe-only) compile model.
      void compile(::Project::Assets::Material &out);

      // Seed an empty graph with one Output sink. Used the first time a
      // .p64mat is created so the user opens it to a non-blank canvas.
      void seedDefaults();
  };
}
