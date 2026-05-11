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
  class MathMod : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_PERCENT " Modulo";

      MathMod()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("A", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("B", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Mod", pinStyle(PinDataType::Float));
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
          // fmodf for floats covers the common Pixic case (positions,
          // animation t). Integer mod should use a separate IntMod
          // node if a callsite needs it; punt for now.
          ctx.line(resVar + " = (float)((" + b + ") != 0 ? fmodf((float)("
                          + a + "), (float)(" + b + ")) : 0);");
        }
      }
  };
}
