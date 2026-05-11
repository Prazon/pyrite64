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
  // Uniform float in [min, max). Backed by libdragon's rand(); seeded
  // once per boot in Scene::Scene so test runs are reproducible across
  // a single power-on. RAND_MAX on this toolchain is at least 32767;
  // the cast through double widens before the divide so the precision
  // doesn't collapse for small ranges.
  class RandomFloat : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_DICE_5 " Random Float";

      RandomFloat()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Min", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        addIN<TypeValue>("Max", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Out", pinStyle(PinDataType::Float));
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
          ctx.line(resVar + " = (float)(" + a + ") + ((float)(" + b
                          + ") - (float)(" + a + ")) * ((float)rand() / (float)RAND_MAX);");
        }
      }
  };
}
