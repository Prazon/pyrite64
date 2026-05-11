/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "nodeMathBinary.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Comparison nodes that return a Bool value pin (composable into
  // BoolAnd / BoolOr chains). The pre-existing Compare node also
  // covers ==/!=/</<= etc. but branches exec instead of returning a
  // value; both forms are useful and supported.
  class CmpEq : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_EQUAL " Equal";

      CmpEq()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("A", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("B", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("A==B", pinStyle(PinDataType::Bool));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("int", resVar, 0);
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto b = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line(resVar + " = ((" + a + ") == (" + b + "));");
        }
      }
  };
}
