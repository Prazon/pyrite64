/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  class Value : public Base
  {
    private:
      uint16_t value{};



    public:
      constexpr static const char* NAME = ICON_MDI_NUMERIC " Value";

      Value()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));

        addOUT<TypeValue>("", PIN_STYLE_VALUE);
      }

      void draw() override {
        ImGui::SetNextItemWidth(50);
        ImGui::InputScalar("##Value", ImGuiDataType_U16, &value);
      }

      void serialize(nlohmann::json &j) override {
        j["value"] = value;
      }

      void deserialize(nlohmann::json &j) override {
        value = j.value("value", 0);
      }

      void build(BuildCtx &ctx) override {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("int", resVar, value);
      }
  };
}