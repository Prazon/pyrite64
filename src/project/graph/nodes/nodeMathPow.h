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

      bool canBePure() const override { return true; }
      void build(BuildCtx &ctx) override { emit(ctx, false); }
      void buildAsPure(BuildCtx &ctx) override { emit(ctx, true); }
    private:
      void emit(BuildCtx &ctx, bool asPure) {
        auto [a, b] = MathHelpers::resolveAB(ctx);
        MathHelpers::emitFloat(ctx, uuid,
          "powf((float)(" + a + "), (float)(" + b + "))", asPure);
      }
  };
}
