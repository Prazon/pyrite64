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
  // printf-style string format with a fixed-size on-stack buffer.
  // Variadic args via dynamic input pins (Add button); each arg pin
  // is generic-typed Wildcard since snprintf accepts any printable
  // primitive. Pixic's score "%05d" path uses this with a single int
  // arg; longer formats add pins as needed.
  class StringFormat : public Base
  {
    private:
      std::string fmt{"%d"};
      uint16_t argCount{1};

    public:
      constexpr static const char* NAME = ICON_MDI_FORMAT_TEXT " Format";

      StringFormat()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        valInputTypes = {0};
        for (uint16_t i = 0; i < argCount; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
          valInputTypes.push_back(1);
        }
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        addOUT<TypeValue>("Out", pinStyle(PinDataType::String));
      }

      void draw() override {
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##fmt", &fmt);
        if (ImGui::Button("Add Arg")) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
          valInputTypes.push_back(1);
          argCount++;
        }
      }

      void serialize(nlohmann::json &j) override {
        j["fmt"] = fmt;
        j["args"] = argCount;
      }

      void deserialize(nlohmann::json &j) override {
        fmt = j.value("fmt", "%d");
        uint16_t saved = j.value("args", uint16_t{1});
        for (uint16_t i = 1; i < saved; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Wildcard));
          valInputTypes.push_back(1);
        }
        argCount = saved;
      }

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("std::string", resVar, std::string{"std::string()"});
        if (!ctx.inValUUIDs) return;
        // Escape the format literal into a C++ string for snprintf.
        std::string escaped;
        escaped.reserve(fmt.size() + 4);
        for (char c : fmt) {
          if (c == '\\' || c == '"') escaped.push_back('\\');
          escaped.push_back(c);
        }
        std::string args;
        for (auto inUUID : *ctx.inValUUIDs) {
          if (inUUID == 0) continue;
          args += ", res_" + Utils::toHex64(inUUID);
        }
        ctx.line("{ char t_buf[128]; std::snprintf(t_buf, sizeof(t_buf), \""
                 + escaped + "\"" + args + "); " + resVar + " = std::string(t_buf); }");
      }
  };
}
