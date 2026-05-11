/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::MaterialGraph::Node
{
  // Sink node. Every .p64mat graph has exactly one of these — its IN pins
  // are the assembly point provider nodes connect to. Owns no fields of
  // its own; contribute() is a no-op.
  class Output : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_FLAG_CHECKERED " Material Output";

      Output()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(::Project::Graph::makeNodeStyle(
          ::Project::Graph::NodeCategory::MaterialGraphRoot));

        // Per-section IN pins. Labels are advisory — connection filter
        // accepts any MatProp output; the compile pass reads from any
        // provider regardless of which slot it's wired into.
        addIN<TypeMatProp>("Color Combiner", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Texture 0",      ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Texture 1",      ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Colors",         ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Render Mode",    ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Geometry",       ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
        addIN<TypeMatProp>("Sampling",       ImFlow::ConnectionFilter::SameType(), PIN_STYLE_MATPROP);
      }

      void draw() override {}

      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void contribute(::Project::Assets::Material &) const override {}
  };
}
