/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Literal string source. The text is editable inline; codegen emits
  // a function-top std::string holding the contents. Pixic uses this
  // for menu labels, prompt text, and the highscore initial format.
  class StringConst : public Base
  {
    public:
      std::string text{};

      constexpr static const char* NAME = ICON_MDI_FORMAT_QUOTE_CLOSE " String";

      StringConst()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addOUT<TypeValue>("", pinStyle(PinDataType::String));
      }

      void draw() override {
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##s", &text);
      }

      void serialize(nlohmann::json &j) override { j["text"] = text; }
      void deserialize(nlohmann::json &j) override { text = j.value("text", ""); }

      void build(BuildCtx &ctx) override {
        auto resVar = "res_" + Utils::toHex64(uuid);
        // Escape backslash + double-quote so arbitrary user text
        // lands inside a C++ string literal without breaking syntax.
        std::string escaped;
        escaped.reserve(text.size() + 4);
        for (char c : text) {
          if (c == '\\' || c == '"') escaped.push_back('\\');
          escaped.push_back(c);
        }
        ctx.globalVar("std::string", resVar, std::string{"std::string(\""} + escaped + "\")");
      }
  };
}
