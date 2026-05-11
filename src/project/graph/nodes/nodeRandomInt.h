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
  // Inclusive integer in [min, max]. The range arg is computed as
  // (max - min + 1) and guarded against zero so swapping the inputs
  // doesn't divide-by-zero. RAND_MAX on libdragon is at least 32767
  // which covers Pixic's match-3 grid coordinates and small sequence
  // indices comfortably.
  class RandomInt : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_DICE_6 " Random Int";

      RandomInt()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Min", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        addIN<TypeValue>("Max", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Out", pinStyle(PinDataType::Int));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("int", resVar, 0);
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto b = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line("{ int t_a = (int)(" + a + "); int t_b = (int)(" + b
                          + "); int t_n = t_b - t_a + 1; "
                          + resVar + " = (t_n > 0) ? (t_a + (rand() % t_n)) : t_a; }");
        }
      }
  };
}
