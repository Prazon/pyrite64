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
  // Variadic array literal. v1 supports std::vector<float> only; the
  // element-kind selector (Int / Bool / String) lands when prefab var
  // ARRAY plumbing is wired (see graph-gaps.md). Add input pins via
  // the Add button; SwitchCase's dynamic-pin pattern.
  class ArrayMake : public Base
  {
    private:
      uint16_t inputCount{2};

    public:
      constexpr static const char* NAME = ICON_MDI_VIEW_LIST " Array Make";

      ArrayMake()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        valInputTypes = {0};
        for (uint16_t i = 0; i < inputCount; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
          valInputTypes.push_back(1);
        }
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Array", pinStyle(PinDataType::Wildcard));
      }

      void draw() override {
        if (ImGui::Button("Add Element")) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
          valInputTypes.push_back(1);
          inputCount++;
        }
      }

      void serialize(nlohmann::json &j) override { j["inputs"] = inputCount; }

      void deserialize(nlohmann::json &j) override {
        uint16_t saved = j.value("inputs", uint16_t{2});
        for (uint16_t i = 2; i < saved; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
          valInputTypes.push_back(1);
        }
        inputCount = saved;
      }

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("std::vector<float>", resVar, std::string{"{}"});
        ctx.line(resVar + ".clear();");
        if (!ctx.inValUUIDs) return;
        for (auto inUUID : *ctx.inValUUIDs) {
          if (inUUID == 0) continue;
          ctx.line(resVar + ".push_back((float)(res_" + Utils::toHex64(inUUID) + "));");
        }
      }
  };
}
