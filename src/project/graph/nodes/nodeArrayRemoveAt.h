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
  class ArrayRemoveAt : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_PLAYLIST_REMOVE " Array RemoveAt";

      ArrayRemoveAt()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        addIN<TypeValue>("Idx", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        valInputTypes = {0, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto i = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          ctx.line("{ size_t t_i = (size_t)((int)(" + i + ")); "
                   "if (t_i < (" + a + ").size()) (" + a + ").erase((" + a + ").begin() + t_i); }");
        }
      }
  };
}
