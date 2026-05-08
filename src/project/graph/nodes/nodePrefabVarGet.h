#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../project/scene/prefab.h"

namespace Project::Graph::Node
{
  /**
   * Reads a class variable from the host prefab. The dropdown lists the
   * prefab's PrefabVarDef entries; the node persists the variable's stable
   * uuid so renames in the editor don't break saved graphs.
   *
   * Codegen emits `auto val_<id> = self->getPrefabVar<T>(UUID);` where T
   * is derived from the variable's PrefabVarKind, then a value-output pin
   * carries that local to downstream nodes.
   */
  class PrefabVarGet : public Base
  {
    public:
      uint64_t varUUID{0};
      // Cached display name + kind so the title and codegen still work
      // when the host prefab is unavailable (standalone NodeEditor).
      std::string varName{};
      uint8_t varKind{0};

      constexpr static const char* NAME = ICON_MDI_VARIABLE " Get Variable";

      void updateTitle() {
        if (varName.empty()) {
          setTitle(NAME);
        } else {
          setTitle(std::string{ICON_MDI_VARIABLE " Get "} + varName);
        }
      }

      PrefabVarGet()
      {
        uuid = Utils::Hash::randomU64();
        updateTitle();
        // Brown to match the value pin colour.
        setStyle(std::make_shared<ImFlow::NodeStyle>(IM_COL32(0xCC, 0x88, 0x55, 0xFF), ImColor(0,0,0,255), 3.5f));
        addOUT<TypeValue>("", PIN_STYLE_VALUE);
      }

      void draw() override
      {
        auto &pctx = activePrefabCtx();
        ImGui::SetNextItemWidth(140.0f);

        if (pctx.prefab && !pctx.prefab->variables.empty()) {
          const char* preview = varName.empty() ? "(select variable)" : varName.c_str();
          if (ImGui::BeginCombo("##v", preview)) {
            for (const auto &v : pctx.prefab->variables) {
              bool sel = (v.uuid == varUUID);
              if (ImGui::Selectable(v.name.c_str(), sel)) {
                varUUID = v.uuid;
                varName = v.name;
                varKind = static_cast<uint8_t>(v.kind);
                updateTitle();
              }
            }
            ImGui::EndCombo();
          }
        } else {
          ImGui::TextDisabled("%s", varName.empty() ? "(no var)" : varName.c_str());
        }
      }

      void serialize(nlohmann::json &j) override {
        j["varUUID"] = varUUID;
        j["varName"] = varName;
        j["varKind"] = varKind;
      }

      void deserialize(nlohmann::json &j) override {
        varUUID = j.value("varUUID", uint64_t{0});
        varName = j.value("varName", "");
        varKind = j.value("varKind", uint8_t{0});
        updateTitle();
      }

      static const char* kindToCType(uint8_t k)
      {
        // Mirrors prefabBuilder.cpp's kindToType. Must stay in sync — the
        // generated POD struct uses these exact types.
        switch (k) {
          case 0: return "int32_t";          // INT
          case 1: return "float";            // FLOAT
          case 2: return "bool";             // BOOL
          case 3: return "fm_vec3_t";        // VEC3
          case 4: return "fm_quat_t";        // QUAT
          case 5: case 6: case 7: return "uint64_t"; // OBJECT_REF/PREFAB_REF/ASSET_REF
        }
        return "int32_t";
      }

      void build(BuildCtx &ctx) override {
        if (varUUID == 0) {
          ctx.line("/* PrefabVarGet: empty variable — skipped */");
          return;
        }
        std::string localName = "v_" + Utils::toHex64(uuid);
        std::string cType = kindToCType(varKind);
        ctx.line(std::string{cType} + " " + localName
                 + " = self->getPrefabVar<" + cType + ">("
                 + std::to_string(varUUID) + "ull);");
      }
  };
}
