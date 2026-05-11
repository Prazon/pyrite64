/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Range-for over a vector value. v1 iterates the array exposed on
  // the Arr input pin (typed Wildcard so it accepts both ArrayMake
  // and any future array-typed prefab var via PrefabVarGet). The
  // current element is exposed on the Element value pin; v1 does
  // not expose a separate Index pin (multi-value-out support is in
  // graph-gaps.md). Use a Counter prefab var inside the body if you
  // need an index.
  class ForEach : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_REPEAT_VARIANT " For Each";

      ForEach()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::ExecSequence));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Arr", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("Body", PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("Done", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Elem", pinStyle(PinDataType::Float));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      bool isLoop() const override { return true; }

      void build(BuildCtx &ctx) override {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, 0.0f);
      }

      void buildLoopHeader(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        std::string a = "(*(decltype(" + resVar + ")*)nullptr)"; // unreachable fallback
        if (ctx.inValUUIDs && !ctx.inValUUIDs->empty() && ctx.inValUUIDs->at(0)) {
          a = "res_" + Utils::toHex64(ctx.inValUUIDs->at(0));
        }
        ctx.line("for (size_t t_i_" + Utils::toHex64(uuid)
                 + " = 0; t_i_" + Utils::toHex64(uuid)
                 + " < (" + a + ").size(); ++t_i_" + Utils::toHex64(uuid) + ") {");
        ctx.line("  " + resVar + " = (" + a + ")[t_i_" + Utils::toHex64(uuid) + "];");
      }
  };
}
