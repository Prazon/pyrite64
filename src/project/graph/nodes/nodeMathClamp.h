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

      bool canBePure() const override { return true; }
      void build(BuildCtx &ctx) override { emit(ctx, false); }
      void buildAsPure(BuildCtx &ctx) override { emit(ctx, true); }
    private:
      void emit(BuildCtx &ctx, bool asPure) {
        std::string v = "0", mn = "0", mx = "0";
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 3) {
          v  = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          mn = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          mx = MathHelpers::resOrZero(ctx.inValUUIDs->at(2));
        }
        std::string expr = "fmaxf((float)(" + mn + "), fminf((float)("
                         + v + "), (float)(" + mx + ")))";
        MathHelpers::emitFloat(ctx, uuid, expr, asPure);
      }
  };
}
