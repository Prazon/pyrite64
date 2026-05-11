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
  // Removes and returns the last element. Empty arrays return 0
  // without underflowing.
  class ArrayPop : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_PLAYLIST_MINUS " Array Pop";

      ArrayPop()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Val", pinStyle(PinDataType::Float));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, 0.0f);
        if (ctx.inValUUIDs && !ctx.inValUUIDs->empty()) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          ctx.line("if (!(" + a + ").empty()) { "
                   + resVar + " = (float)((" + a + ").back()); (" + a + ").pop_back(); } else { "
                   + resVar + " = 0.0f; }");
        }
      }
  };
}
