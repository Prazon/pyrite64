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
  // Variadic string concatenation. New input pins are added via the
  // "Add" button; deserialize replays the count so saved graphs round-
  // trip cleanly. Mirrors SwitchCase's dynamic-pin pattern.
  class StringConcat : public Base
  {
    private:
      uint16_t inputCount{2};

    public:
      constexpr static const char* NAME = ICON_MDI_FORMAT_TEXT " Concat";

      StringConcat()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        valInputTypes = {0};
        for (uint16_t i = 0; i < inputCount; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::String));
          valInputTypes.push_back(1);
        }
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Out", pinStyle(PinDataType::String));
      }

      void draw() override {
        if (ImGui::Button("Add")) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::String));
          valInputTypes.push_back(1);
          inputCount++;
        }
      }

      void serialize(nlohmann::json &j) override { j["inputs"] = inputCount; }

      void deserialize(nlohmann::json &j) override {
        // Replay any extra pins beyond the ctor's default of 2.
        uint16_t saved = j.value("inputs", uint16_t{2});
        for (uint16_t i = 2; i < saved; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::String));
          valInputTypes.push_back(1);
        }
        inputCount = saved;
      }

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("std::string", resVar, std::string{"std::string()"});
        if (!ctx.inValUUIDs) return;
        std::string expr;
        bool first = true;
        for (auto inUUID : *ctx.inValUUIDs) {
          if (inUUID == 0) continue;
          if (!first) expr += " + ";
          expr += "res_" + Utils::toHex64(inUUID);
          first = false;
        }
        if (!expr.empty()) {
          ctx.line(resVar + " = " + expr + ";");
        }
      }
  };
}
