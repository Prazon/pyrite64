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
  // Array index read. Out-of-range returns 0 to keep the runtime
  // free of std::out_of_range exceptions on bad indices.
  class ArrayGet : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_FORMAT_LIST_NUMBERED " Array Get";

      ArrayGet()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        addIN<TypeValue>("Idx", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        valInputTypes = {0, 1, 1};
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
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto i = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line("{ size_t t_i = (size_t)((int)(" + i + ")); "
                   + resVar + " = (t_i < (" + a + ").size()) ? (" + a + ")[t_i] : 0.0f; }");
        }
      }
  };
}
