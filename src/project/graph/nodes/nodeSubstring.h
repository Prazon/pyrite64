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
  // Wraps std::string::substr with a clamped start so a runaway index
  // doesn't throw out_of_range on the runtime. Pixic uses this for
  // the highscore name initial display ("ABC" -> "A").
  class Substring : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_FORMAT_TEXT " Substring";

      Substring()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Str",   ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::String));
        addIN<TypeValue>("Start", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        addIN<TypeValue>("Len",   ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        valInputTypes = {0, 1, 1, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Sub", pinStyle(PinDataType::String));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("std::string", resVar, std::string{"std::string()"});
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 3) {
          auto s = MathHelpers::resOrZero(ctx.inValUUIDs->at(0));
          auto a = MathHelpers::resOrZero(ctx.inValUUIDs->at(1));
          auto n = MathHelpers::resOrZero(ctx.inValUUIDs->at(2));
          // Clamp start to [0, len] to avoid std::out_of_range; len
          // gets passed as-is and substr internally clamps the tail.
          ctx.line("{ size_t t_len = (" + s + ").length(); "
                   "size_t t_st = (size_t)((int)(" + a + ") < 0 ? 0 : (int)(" + a + ")); "
                   "if (t_st > t_len) t_st = t_len; "
                   + resVar + " = (" + s + ").substr(t_st, (size_t)(" + n + ")); }");
        }
      }
  };
}
