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
  // Numeric -> string via std::to_string. The value pin is typed as
  // Float since that's the most common numeric in this graph (with
  // implicit promotion from Int / Bool). For string-passthrough use
  // a Concat with a single input.
  class ToString : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_NUMERIC_POSITIVE_1 " ToString";

      ToString()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("X", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Str", pinStyle(PinDataType::String));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("std::string", resVar, std::string{"std::string()"});
        if (ctx.inValUUIDs && !ctx.inValUUIDs->empty()) {
          auto x = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          ctx.line(resVar + " = std::to_string((float)(" + x + "));");
        }
      }
  };
}
