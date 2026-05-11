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
  // Counted for loop. Body subgraph is inlined inside a real C++ for
  // by the build pass (loop ownership pre-walk in graph.cpp). The
  // current iteration index is exposed on a value pin so body nodes
  // can read it via the standard res_<uuid> convention.
  //
  // Pin layout chosen to match Repeat / SwitchCase:
  //   IN:  exec, Start (Int), End (Int), Step (Int)
  //   OUT: Body (exec), Done (exec), Index (Int value)
  class ForRange : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_REPEAT_VARIANT " For Range";

      ForRange()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::ExecSequence));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Start", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        addIN<TypeValue>("End",   ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        addIN<TypeValue>("Step",  ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Int));
        valInputTypes = {0, 1, 1, 1};
        addOUT<TypeLogic>("Body", PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("Done", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Idx",  pinStyle(PinDataType::Int));
      }

      void draw() override {}
      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      bool isLoop() const override { return true; }

      void build(BuildCtx &ctx) override {
        // Index value pin storage. Default 0; the for header below
        // overwrites it on every iteration so body nodes that read
        // res_<uuid> see the current iteration index.
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("int", resVar, 0);
      }

      // Loop pass calls these around the inlined body emission.
      void buildLoopHeader(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        std::string s = "0", e = "0", st = "1";
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 3) {
          if (ctx.inValUUIDs->at(0)) s  = "res_" + Utils::toHex64(ctx.inValUUIDs->at(0));
          if (ctx.inValUUIDs->at(1)) e  = "res_" + Utils::toHex64(ctx.inValUUIDs->at(1));
          if (ctx.inValUUIDs->at(2)) st = "res_" + Utils::toHex64(ctx.inValUUIDs->at(2));
        }
        ctx.line("for (int t_i_" + Utils::toHex64(uuid)
                 + " = (int)(" + s + "); t_i_" + Utils::toHex64(uuid)
                 + " < (int)(" + e + "); t_i_" + Utils::toHex64(uuid)
                 + " += (int)(" + st + ")) {");
        ctx.line("  " + resVar + " = t_i_" + Utils::toHex64(uuid) + ";");
      }
  };
}
