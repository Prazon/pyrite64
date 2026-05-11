/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Emits a C++ break;. Valid only inside a loop body; outside, the
  // generated code will fail to compile with a clear "break used
  // outside of loop" diagnostic from the host compiler. The build
  // pass marks loop body nodes; Break placed outside any loop body
  // would still emit the keyword but the surrounding scope is the
  // function body, hence the compile error.
  class Break : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_STOP " Break";

      Break()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::ExecBranch));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override {
        ctx.line("break;");
      }
  };
}
