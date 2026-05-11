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
  class ArrayContains : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_TEXT_SEARCH " Array Contains";

      ArrayContains()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        addIN<TypeValue>("Val", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Has", pinStyle(PinDataType::Bool));
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
          auto v = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line(resVar + " = 0;");
          ctx.line("for (size_t t_i = 0; t_i < (" + a + ").size(); ++t_i) "
                   "if ((" + a + ")[t_i] == (float)(" + v + ")) { "
                   + resVar + " = 1; break; }");
        }
      }
  };
}
