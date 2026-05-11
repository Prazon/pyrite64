#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"
#include "../../../project/scene/prefab.h"
#include "../../../project/prefabScaffolder.h"

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
      // Element kind for ARRAY-typed vars (mirrors PrefabVarKind: 0=Int,
      // 1=Float, 2=Bool). Only consulted when varKind == 8 (ARRAY); the
      // emitted std::vector<E> picks E from this. Synced from the prefab's
      // PrefabVarDef.typeArg when the var is bound via the dropdown.
      uint8_t elemKind{1};

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
          case 8: return PinDataType::Wildcard; // ARRAY (color matches array nodes)
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
                // Sync elemKind from PrefabVarDef.typeArg so ARRAY codegen
                // emits the right std::vector<E> without a second click.
                elemKind = v.typeArg;
                updateTitle();
              }
            }
            ImGui::EndCombo();
          }
        } else {
          ImGui::TextDisabled("%s", varName.empty() ? "(no var)" : varName.c_str());
        }
        // ARRAY-kind vars surface a small "(vector<E>)" hint so users
        // can sanity-check the element type without opening the prefab
        // variables panel. Kind 8 == ARRAY.
        if (varKind == 8) {
          const char* lbl = "?";
          switch (elemKind) {
            case 0: lbl = "int"; break;
            case 1: lbl = "float"; break;
            case 2: lbl = "bool"; break;
          }
          ImGui::SameLine();
          ImGui::TextDisabled("(vector<%s>)", lbl);
        }
        // Tier 2 autofix: if the node references a variable uuid that
        // the host prefab no longer defines (rename / delete on a
        // saved graph), offer a one-click backfill. The new VarDef
        // reuses the saved uuid so existing nodes stay bound and the
        // generated POD struct picks the slot back up on the next
        // build.
        if (pctx.prefab && varUUID != 0) {
          bool found = false;
          for (const auto &v : pctx.prefab->variables) {
            if (v.uuid == varUUID) { found = true; break; }
          }
          if (!found) {
            ImGui::PushStyleColor(ImGuiCol_Button,
              IM_COL32(0xC0, 0x70, 0x30, 0xFF));
            if (ImGui::SmallButton("Create variable")) {
              ::Project::PrefabScaffolder::UnknownVarRef ref;
              ref.varName  = varName;
              ref.varKind  = varKind;
              ref.varUUID  = varUUID;
              ref.nodeUUID = uuid;
              // pctx.prefab is exposed as const* to discourage casual
              // mutation from node draw paths; the autofix is an explicit
              // user action so the cast is intentional.
              auto *mutPrefab = const_cast<::Project::Prefab*>(pctx.prefab);
              ::Project::PrefabScaffolder::autofixVariables(
                *mutPrefab, {ref});
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(missing)");
          }
        }
      }

      void serialize(nlohmann::json &j) override {
        j["varUUID"] = varUUID;
        j["varName"] = varName;
        j["varKind"] = varKind;
        j["elemKind"] = elemKind;
      }

      void deserialize(nlohmann::json &j) override {
        varUUID = j.value("varUUID", uint64_t{0});
        varName = j.value("varName", "");
        varKind = j.value("varKind", uint8_t{0});
        elemKind = j.value("elemKind", uint8_t{1});
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

      // Element-type C name for ARRAY kind. Matches ArrayMake's encoding:
      // 0=int32_t, 1=float, 2=bool. Kept here so ARRAY codegen stays
      // self-contained without a cross-include into nodeArrayMake.h.
      static const char* arrayElemCType(uint8_t e) {
        switch (e) {
          case 0: return "int32_t";
          case 1: return "float";
          case 2: return "bool";
        }
        return "float";
      }

      void build(BuildCtx &ctx) override {
        if (varUUID == 0) {
          ctx.line("/* PrefabVarGet: empty variable — skipped */");
          return;
        }
        std::string resVar = "res_" + Utils::toHex64(uuid);
        if (varKind == 8) {
          // ARRAY: getPrefabVarRefChecked asserts kind/elemKind match
          // before reinterpreting the bytes, so a stale graph against
          // a renamed-out-from-under-it prefab var fires loudly at
          // first read instead of corrupting the vector silently.
          // Emit a reference (auto&) so downstream array nodes mutate
          // the actual storage rather than a copy. Persists across
          // OnTick dispatches because the storage lives on the Object.
          std::string vecType = std::string{"std::vector<"} + arrayElemCType(elemKind) + ">";
          ctx.line("auto& " + resVar + " = *self->getPrefabVarRefChecked<" + vecType + ">("
                   + std::to_string(varUUID) + "ull, 8, "
                   + std::to_string((int)elemKind) + ");");
        } else {
          std::string cType = kindToCType(varKind);
          ctx.line(std::string{cType} + " " + resVar
                   + " = self->getPrefabVar<" + cType + ">("
                   + std::to_string(varUUID) + "ull);");
        }
      }
  };
}
