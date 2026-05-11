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
  class MathSign : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_FUNCTION " Sign";

      MathSign()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("X", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Sign", pinStyle(PinDataType::Float));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      bool canBePure() const override { return true; }
      void build(BuildCtx &ctx) override { emit(ctx, false); }
      void buildAsPure(BuildCtx &ctx) override { emit(ctx, true); }
    private:
      void emit(BuildCtx &ctx, bool asPure) {
        auto x = MathHelpers::resolveA(ctx);
        // Three-way sign: -1 / 0 / +1. Matches PICO-8's sgn semantics.
        std::string expr = "((" + x + ") > 0.0f) - ((" + x + ") < 0.0f)";
        MathHelpers::emitFloat(ctx, uuid, expr, asPure);
      }
  };
}
