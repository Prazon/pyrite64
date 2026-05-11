/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "ImNodeFlow.h"
#include "json.hpp"
#include "IconsMaterialDesignIcons.h"

#include "../../assets/material.h"
#include "../../graph/nodeStyles.h"

namespace Project::MaterialGraph::Node
{
  // Single unified pin style for "MatProp" connections. Provider nodes
  // each expose one OUT pin; the MaterialOutput sink exposes N IN pins,
  // one per material section. Order of contribution doesn't matter — see
  // Graph::compile().
  extern std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_MATPROP;

  // Tag type for ImNodeFlow's connection filtering.
  struct TypeMatProp { };

  class Base : public ImFlow::BaseNode
  {
    public:
      uint64_t uuid{};
      uint32_t type{};

      virtual void serialize(nlohmann::json &j) = 0;
      virtual void deserialize(nlohmann::json &j) = 0;

      // Called during Graph::compile() when this node is reachable from
      // the MaterialOutput sink. Implementations should mutate `out` with
      // the fields they own. Multiple connected nodes overlay; later
      // visited nodes win for any conflicting fields. v1 has no diagnostic
      // for conflicts — node organisation is the user's responsibility.
      virtual void contribute(::Project::Assets::Material &out) const = 0;
  };
}
