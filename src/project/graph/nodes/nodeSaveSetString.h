/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "nodeSaveCommon.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  class SaveSetString : public Base
  {
    private:
      uint64_t groupUUID{0};
      std::string fieldName{};

    public:
      constexpr static const char* NAME = ICON_MDI_DATABASE_ARROW_RIGHT " Save Set String";

      SaveSetString()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addIN<TypeValue>("Value", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::String));
        valInputTypes = {0, 1};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override {
        SaveHelpers::drawSelectors(groupUUID, fieldName,
          (int)::Project::Assets::SaveFileAsset::FT_STRING);
      }

      void serialize(nlohmann::json &j) override {
        j["groupUUID"] = groupUUID;
        j["field"]     = fieldName;
      }
      void deserialize(nlohmann::json &j) override {
        groupUUID = j.value<uint64_t>("groupUUID", 0);
        fieldName = j.value<std::string>("field", "");
      }

      void build(BuildCtx &ctx) override {
        std::string call = SaveHelpers::resolveCall(groupUUID, fieldName, "set");
        if (call.empty()) return;
        std::string val = "\"\"";
        if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2 && (*ctx.inValUUIDs)[1] != 0) {
          // String inputs are std::string; pass .c_str() to the const char* setter.
          val = std::string("res_") + Utils::toHex64((*ctx.inValUUIDs)[1]) + ".c_str()";
        }
        ctx.line("Game::Save::" + call + "(" + val + ");");
      }
  };
}
