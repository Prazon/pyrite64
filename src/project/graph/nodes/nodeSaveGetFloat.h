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
  class SaveGetFloat : public Base
  {
    private:
      uint64_t groupUUID{0};
      std::string fieldName{};

    public:
      constexpr static const char* NAME = ICON_MDI_DATABASE_ARROW_LEFT " Save Get Float";

      SaveGetFloat()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        valInputTypes = {0};
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Value", pinStyle(PinDataType::Float));
      }

      void draw() override {
        SaveHelpers::drawSelectors(groupUUID, fieldName,
          (int)::Project::Assets::SaveFileAsset::FT_FLOAT);
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
        std::string resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, 0.0f);
        std::string call = SaveHelpers::resolveCall(groupUUID, fieldName, "get");
        if (!call.empty()) {
          ctx.line(resVar + " = Game::Save::" + call + "();");
        }
      }
  };
}
