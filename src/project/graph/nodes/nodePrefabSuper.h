#pragma once

#include "baseNode.h"
#include "nodePrefabEvent.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  /**
   * Calls into the parent prefab's dispatch for a chosen event kind —
   * Unreal Blueprint's "Call Parent Function" / Super:: pattern.
   *
   * Codegen emits a placeholder that the prefab-aware codegen pass in
   * src/build/prefabBuilder.cpp replaces with a qualified call to the
   * parent's generated `dispatch_<ParentIdent>(self, <evtId>, deltaTime)`.
   * Empty parent dispatcher (parent has no graph) compiles to a no-op
   * thanks to the weak symbol in prefabEvents.h.
   *
   * The {{PARENT_DISPATCH}} placeholder lets codegen resolve the parent's
   * dispatcher name once it knows the prefab's parent chain — same
   * mechanism PrefabFunc uses for {{PFX}}.
   */
  class PrefabSuper : public Base
  {
    public:
      PrefabEvent::Kind kind{PrefabEvent::Kind::Ready};

      constexpr static const char* NAME = ICON_MDI_ARROW_UP_THIN " Super";

      void updateTitle() {
        setTitle(std::string{ICON_MDI_ARROW_UP_THIN " Super::"}
                 + PrefabEvent::kindLabel(kind));
      }

      PrefabSuper()
      {
        uuid = Utils::Hash::randomU64();
        updateTitle();
        setStyle(makeNodeStyle(NodeCategory::FunctionCall));
        addIN<TypeLogic>("", ImFlow::ConnectionFilter::SameType(), PIN_STYLE_LOGIC);
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override
      {
        static const char* labels[] = {
          "OnReady", "OnEnable", "OnDisable",
          "OnCustom0", "OnCustom1", "OnCustom2", "OnCustom3",
          "OnTick",
        };
        int idx = static_cast<int>(kind);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("##superKind", &idx, labels, IM_ARRAYSIZE(labels))) {
          kind = static_cast<PrefabEvent::Kind>(idx);
          updateTitle();
        }
      }

      void serialize(nlohmann::json &j) override {
        j["kind"] = static_cast<int>(kind);
      }

      void deserialize(nlohmann::json &j) override {
        kind = static_cast<PrefabEvent::Kind>(j.value("kind", 0));
        updateTitle();
      }

      void build(BuildCtx &ctx) override {
        // {{PARENT_DISPATCH}} is substituted at codegen time once the
        // parent ident is known. If the prefab has no parent it expands
        // to an empty string and the codegen wraps the line out.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "0x%04Xu",
          (unsigned)PrefabEvent::kindEventId(kind));
        ctx.line(std::string{"{{PARENT_DISPATCH}}(self, "} + buf + ", deltaTime);");
      }
  };
}
