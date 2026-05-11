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
  // In-place array element write. Out-of-range index is silently
  // ignored. The Arr input pin is required to reference an array-
  // typed value (ArrayMake output or a future array prefab var via
  // PrefabVarGet) so the assignment lands on the actual storage.
  class ArraySet : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_FORMAT_LIST_NUMBERED " Array Set";

      ArraySet()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        addIN<TypeValue>("Idx", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        addIN<TypeValue>("Val", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
        valInputTypes = {0, 1, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 3) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto i = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          auto v = MathHelpers::resOrZero(ctx.inValUUIDs->at(2));
          ctx.line("{ size_t t_i = (size_t)((int)(" + i + ")); "
                   "if (t_i < (" + a + ").size()) (" + a + ")[t_i] = (float)(" + v + "); }");
        }
      }
  };
}
