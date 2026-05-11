#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../project/prefabFunctions.h"

namespace Project::Graph::Node
{
  /**
   * Calls a P64_NODE-tagged C++ function from src/user/<prefab>.{h,cpp}.
   * The dropdown lists the discovered functions (Phase 2 scanner output);
   * when there's no active prefab context, falls back to a free-text input
   * so standalone NodeEditor sessions don't break.
   *
   * Codegen emits `User::<Ident>::<funcName>(self);` and chains to the
   * next logic-out target.
   */
  class PrefabFunc : public Base
  {
    public:
      std::string funcName{};

      constexpr static const char* NAME = ICON_MDI_FUNCTION " Prefab Function";

      void updateTitle() {
        if (funcName.empty()) {
          setTitle(NAME);
        } else {
          setTitle(std::string{ICON_MDI_FUNCTION " "} + funcName);
        }
      }

      PrefabFunc()
      {
        uuid = Utils::Hash::randomU64();
        updateTitle();
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override
      {
        auto &pctx = activePrefabCtx();
        ImGui::SetNextItemWidth(160.0f);

        if (!pctx.prefabName.empty() && !pctx.projectPath.empty()) {
          // Active prefab: scan for P64_NODE functions in the user header
          // and offer them as a dropdown. Re-scan each frame so live edits
          // to the .h file appear without an explicit refresh.
          auto funcs = ::Project::scanPrefabFunctions(pctx.projectPath, pctx.prefabName);
          const char* preview = funcName.empty() ? "(select function)" : funcName.c_str();
          if (ImGui::BeginCombo("##fn", preview)) {
            for (const auto &f : funcs) {
              bool sel = (f.name == funcName);
              if (ImGui::Selectable(f.name.c_str(), sel)) {
                funcName = f.name;
                updateTitle();
              }
            }
            ImGui::EndCombo();
          }
        } else {
          if (ImGui::InputText("##fn", &funcName)) {
            updateTitle();
          }
        }
      }

      void serialize(nlohmann::json &j) override {
        j["funcName"] = funcName;
      }

      void deserialize(nlohmann::json &j) override {
        funcName = j.value("funcName", "");
        updateTitle();
      }

      void build(BuildCtx &ctx) override {
        if (funcName.empty()) {
          ctx.line("/* PrefabFunc: empty funcName — skipped */");
          return;
        }
        // Codegen relies on the user-namespace shape produced by the
        // Phase 2 scaffold: namespace User::<sanitizedPrefab>. The actual
        // name expansion happens at codegen time when the prefab name is
        // known — here we just emit a placeholder that the prefab-aware
        // codegen pass replaces. {{PFX}} is substituted in
        // PrefabBuilder::buildPrefabEventGraphs.
        ctx.line("User::{{PFX}}::" + funcName + "(self);");
      }
  };
}
