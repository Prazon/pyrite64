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

      // Title bar colour by variable kind. Matches the pill palette in
      // PrefabEditor's My-Prefab Variables panel so the node visually
      // identifies which kind it carries at a glance (UE-Blueprint style).
      static ImU32 kindToColor(uint8_t k) {
        switch (k) {
          case 0: return IM_COL32( 77, 204, 217, 255); // INT        — cyan
          case 1: return IM_COL32(115, 217,  77, 255); // FLOAT      — green
          case 2: return IM_COL32(217,  51,  51, 255); // BOOL       — red
          case 3: return IM_COL32(242, 217,  64, 255); // VEC3       — yellow
          case 4: return IM_COL32(242, 140,  51, 255); // QUAT       — orange
          case 5: return IM_COL32( 77, 140, 242, 255); // OBJECT_REF — blue
          case 6: return IM_COL32(217,  77, 217, 255); // PREFAB_REF — magenta
          case 7: return IM_COL32(166, 166, 166, 255); // ASSET_REF  — grey
        }
        // Unset / empty: keep the original brown so an unbound node is
        // visually distinct from any kind-coloured one.
        return IM_COL32(0xCC, 0x88, 0x55, 255);
      }

      // Per-instance pin style so the output socket + the wires drawn from
      // it can be tinted by the variable's kind (Unreal-Blueprint behavior:
      // wires inherit the source pin's colour). Falls back to the global
      // brown when no variable is bound yet.
      std::shared_ptr<ImFlow::PinStyle> pinStyle{};

      void applyKindStyle() {
        const bool bound = !varName.empty();
        const ImU32 kindCol = bound ? kindToColor(varKind)
                                    : IM_COL32(0xCC, 0x88, 0x55, 255);

        // Unreal "Get" node aesthetic: a saturated kind-coloured header bar
        // over a darker body. Going uniform-kind-colour for the whole node
        // makes the output socket disappear against the body — the socket
        // sits on the right edge so half the circle overlaps the body fill.
        // Darkening the body restores the contrast while keeping the pill
        // shape and kind-colour identity. Body tint = kindCol * 0.30 so the
        // hue still reads but the brightness drops well below the pin.
        auto darken = [](ImU32 col, float k) -> ImU32 {
          int r = (int)((col >>  0) & 0xFF);
          int g = (int)((col >>  8) & 0xFF);
          int b = (int)((col >> 16) & 0xFF);
          r = (int)(r * k); g = (int)(g * k); b = (int)(b * k);
          return IM_COL32(r, g, b, 255);
        };
        const ImU32 bodyCol = darken(kindCol, 0.30f);

        auto ns = std::make_shared<ImFlow::NodeStyle>(
          kindCol, ImColor(0, 0, 0, 255), 8.0f
        );
        ns->bg            = bodyCol;
        ns->border_color  = IM_COL32(0, 0, 0, 200);
        ns->padding       = ImVec4(10.0f, 4.0f, 10.0f, 4.0f);
        setStyle(std::move(ns));

        // Mutate the pin style in place so the existing OutPin (created in
        // the ctor) and every link drawn from it pick up the new colour
        // without rebuilding the pin. Pin keeps full saturation so it pops
        // against the darker body half it overlaps.
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
