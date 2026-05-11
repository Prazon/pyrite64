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
  class MathClamp : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_ARROW_COLLAPSE_HORIZONTAL " Clamp";

      MathClamp()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Value", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("Min",   ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("Max",   ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Clamped", pinStyle(PinDataType::Float));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, 0.0f);
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 3) {
          auto v   = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto mn  = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          auto mx  = MathHelpers::resOrZero(ctx.inValUUIDs->at(2));
          ctx.line(resVar + " = fmaxf((float)(" + mn + "), fminf((float)("
                          + v + "), (float)(" + mx + ")));");
        }
      }
  };
}
