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
  class MathPow : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_EXPONENT " Pow";

      MathPow()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Base", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("Exp",  ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Pow", pinStyle(PinDataType::Float));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, 0.0f);
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto b = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line(resVar + " = powf((float)(" + a + "), (float)(" + b + "));");
        }
      }
  };
}
