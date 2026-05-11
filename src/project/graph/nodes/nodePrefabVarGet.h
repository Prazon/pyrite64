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

      // Map a PrefabVarKind to the canonical UE5 PinDataType so this node
      // shares wires/colours with everything else in the graph. The
      // unbound case stays as wildcard grey, matching UE's "untyped" pin.
      static PinDataType kindToPinType(uint8_t k) {
        switch (k) {
          case 0: return PinDataType::Int;     // INT
          case 1: return PinDataType::Float;   // FLOAT
          case 2: return PinDataType::Bool;    // BOOL
          case 3: return PinDataType::Struct;  // VEC3 (UE5 colours FVector blue)
          case 4: return PinDataType::Rotator; // QUAT
          case 5: return PinDataType::Object;  // OBJECT_REF
          case 6: return PinDataType::Class;   // PREFAB_REF
          case 7: return PinDataType::Object;  // ASSET_REF
        }
        return PinDataType::Wildcard;
      }

      static ImU32 kindToColor(uint8_t k) { return pinColor(kindToPinType(k)); }

      // Per-instance pin style so the output socket + the wires drawn from
      // it can be tinted by the variable's kind (Unreal-Blueprint behavior:
      // wires inherit the source pin's colour). Falls back to the global
      // brown when no variable is bound yet.
      std::shared_ptr<ImFlow::PinStyle> pinStyle{};

      void applyKindStyle() {
        // Pure-function pill (Variable Get). Header carries the kind
        // colour; body is the canonical dark grey from makeNodeStyle.
        // Mutating the existing pin style in place keeps the wires drawn
        // from this output in the same colour without rebuilding pins.
        auto ns = makeNodeStyle(NodeCategory::PureFunctionCall);
        const ImU32 kindCol = !varName.empty()
          ? kindToColor(varKind)
          : pinColor(PinDataType::Wildcard);
        ns->header_bg = kindCol;
        // Re-pick title text contrast for the kind colour.
        const float r = ((kindCol >>  0) & 0xFF) / 255.0f;
        const float g = ((kindCol >>  8) & 0xFF) / 255.0f;
        const float b = ((kindCol >> 16) & 0xFF) / 255.0f;
        const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
        ns->header_title_color = lum > 0.6f
          ? ImColor(20, 20, 20, 255) : ImColor(245, 245, 245, 255);
        setStyle(std::move(ns));

        if (pinStyle) pinStyle->color = kindCol;
      }

      void updateTitle() {
        if (varName.empty()) {
          setTitle(NAME);
        } else {
          setTitle(varName);
        }
        // Restyle on every title refresh — covers ctor, dropdown selection,
        // drag-drop drop, and deserialize without a separate hook.
        applyKindStyle();
      }

      PrefabVarGet()
      {
        uuid = Utils::Hash::randomU64();
        // Seed the per-instance pin style from the global brown so the
        // socket renders correctly even before any kind is selected.
        pinStyle = std::make_shared<ImFlow::PinStyle>(
          IM_COL32(0xCC, 0x88, 0x55, 255), 0, 4.f, 4.67f, 3.7f, 1.f
        );
        updateTitle();
        addOUT<TypeValue>("", pinStyle);
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
