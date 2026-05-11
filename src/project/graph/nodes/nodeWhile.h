/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Condition-checked loop. Body subgraph inlined by the build pass
  // (see ForRange). The condition is re-evaluated each iteration via
  // the value pin's source; mutating the condition's upstream chain
  // inside the body is the standard escape mechanism (or use Break).
  class While : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_REPEAT_VARIANT " While";

      While()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::ExecSequence));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Cond", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Bool));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("Body", PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("Done", PIN_STYLE_LOGIC);
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      bool isLoop() const override { return true; }
      void build(BuildCtx &) override {}

      void buildLoopHeader(BuildCtx &ctx) override
      {
        std::string c = "0";
        if (ctx.inValUUIDs && !ctx.inValUUIDs->empty() && ctx.inValUUIDs->at(0)) {
          c = "res_" + Utils::toHex64(ctx.inValUUIDs->at(0));
        }
        ctx.line("while (" + c + ") {");
      }
  };
}
