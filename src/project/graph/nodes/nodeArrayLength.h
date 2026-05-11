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
  class ArrayLength : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_VIEW_LIST_OUTLINE " Array Length";

      ArrayLength()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Len", pinStyle(PinDataType::Int));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("int", resVar, 0);
        if (ctx.inValUUIDs && !ctx.inValUUIDs->empty()) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          ctx.line(resVar + " = (int)((" + a + ").size());");
        }
      }
  };
}
