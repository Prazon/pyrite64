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
  // Variadic array literal with two authoring modes:
  //   - Pin mode (default, useLiterals=false): one input pin per element,
  //     wired from value sources. Best when elements come from upstream
  //     nodes (formulas, prefab vars).
  //   - Literal mode (useLiterals=true): inline DragFloat per row, no
  //     input pins. Best for static arrays (lookup tables, color
  //     palettes). Toggle via the "Lit" checkbox in the header.
  //
  // The element-kind dropdown picks the std::vector<E> type; defaults
  // to Float. Kind values mirror PrefabVarKind so ArrayMake /
  // PrefabVarGet share a vocabulary: 0=Int, 1=Float, 2=Bool. Non-
  // numeric kinds (string, structs) are deferred until per-pin element
  // typing lands.
  class ArrayMake : public Base
  {
    private:
      uint16_t inputCount{2};

    public:
      uint8_t elemKind{1};   // 0=Int, 1=Float, 2=Bool
      bool useLiterals{false};
      std::vector<float> literalValues{0.0f, 0.0f}; // tracked length == inputCount in literal mode

      constexpr static const char* NAME = ICON_MDI_VIEW_LIST " Array Make";

      static const char* elemKindToCType(uint8_t k) {
        switch (k) {
          case 0: return "int32_t";
          case 1: return "float";
          case 2: return "bool";
        }
        return "float";
      }

      static const char* elemKindLabel(uint8_t k) {
        switch (k) {
          case 0: return "Int";
          case 1: return "Float";
          case 2: return "Bool";
        }
        return "Float";
      }

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
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::BeginCombo("##ek", elemKindLabel(elemKind))) {
          for (uint8_t k = 0; k < 3; ++k) {
            bool sel = (k == elemKind);
            if (ImGui::Selectable(elemKindLabel(k), sel)) elemKind = k;
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Lit", &useLiterals);
        if (useLiterals) {
          // Inline literal table: one DragFloat per element, no pins
          // visible. Compact two-column layout keeps the node body
          // narrow even for ~20-element lookup tables.
          for (uint16_t i = 0; i < inputCount; ++i) {
            if (i >= literalValues.size()) literalValues.push_back(0.0f);
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(80.0f);
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "[%d]", (int)i);
            ImGui::DragFloat(lbl, &literalValues[i], 0.1f);
            ImGui::PopID();
          }
        }
        ImGui::Spacing();
        if (ImGui::SmallButton("+ Element")) {
          if (!useLiterals) {
            addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
            valInputTypes.push_back(1);
          }
          literalValues.push_back(0.0f);
          inputCount++;
        }
        ImGui::SameLine();
        if (inputCount > 0 && ImGui::SmallButton("-")) {
          inputCount--;
          if (!literalValues.empty()) literalValues.pop_back();
          // Pin removal isn't supported by ImNodeFlow today; in pin
          // mode the trailing pin stays on screen (cosmetic) but is
          // skipped by build() because inputCount is the source of
          // truth. Switch to literal mode to author large arrays.
        }
      }

      void serialize(nlohmann::json &j) override {
        j["inputs"]   = inputCount;
        j["elemKind"] = elemKind;
        j["lit"]      = useLiterals;
        j["litVals"]  = literalValues;
      }

      void deserialize(nlohmann::json &j) override {
        uint16_t saved = j.value("inputs", uint16_t{2});
        for (uint16_t i = 2; i < saved; ++i) {
          addIN<TypeValue>("", ImFlow::ConnectionFilter::SameType(), pinStyle(PinDataType::Float));
          valInputTypes.push_back(1);
        }
        inputCount = saved;
        elemKind = j.value("elemKind", uint8_t{1});
        useLiterals = j.value("lit", false);
        if (j.contains("litVals") && j["litVals"].is_array()) {
          literalValues.clear();
          for (const auto &v : j["litVals"]) literalValues.push_back(v.get<float>());
        }
        while (literalValues.size() < inputCount) literalValues.push_back(0.0f);
      }

      void build(BuildCtx &ctx) override
      {
        auto resVar = "res_" + Utils::toHex64(uuid);
        std::string vecType = std::string{"std::vector<"} + elemKindToCType(elemKind) + ">";
        ctx.globalVar(vecType, resVar, std::string{"{}"});
        ctx.line(resVar + ".clear();");
        if (useLiterals) {
          // Emit literal pushes from the inline table, ignoring pins
          // entirely. Keeps generated code compact and avoids needing
          // N upstream Value nodes for a static lookup table.
          for (uint16_t i = 0; i < inputCount && i < literalValues.size(); ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.7gf", static_cast<double>(literalValues[i]));
            ctx.line(resVar + ".push_back("
                     "(typename std::remove_reference_t<decltype(" + resVar + ")>::value_type)("
                     + buf + "));");
          }
          return;
        }
        if (!ctx.inValUUIDs) return;
        for (auto inUUID : *ctx.inValUUIDs) {
          if (inUUID == 0) continue;
          ctx.line(resVar + ".push_back("
                   "(typename std::remove_reference_t<decltype(" + resVar + ")>::value_type)"
                   "(res_" + Utils::toHex64(inUUID) + "));");
        }
      }
  };
}
