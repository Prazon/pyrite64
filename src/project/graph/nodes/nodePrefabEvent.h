#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  /**
   * Event-entry node: an entry point for the prefab's event-graph dispatch.
   * Holds an event ID picked from a known set (OnReady / OnEnable /
   * OnDisable / custom-N). One logic output that fans into the chain of
   * nodes that should run when that event fires on a prefab instance.
   *
   * Codegen treats this node specially — it doesn't emit any code itself;
   * it provides a label that a top-of-function switch jumps to.
   */
  class PrefabEvent : public Base
  {
    public:
      enum class Kind : int {
        Ready    = 0,
        Enable   = 1,
        Disable  = 2,
        Custom0  = 3,
        Custom1  = 4,
        Custom2  = 5,
        Custom3  = 6,
      };

      Kind kind{Kind::Ready};

      constexpr static const char* NAME = ICON_MDI_PLAY " Event";

      static const char* kindLabel(Kind k)
      {
        switch (k) {
          case Kind::Ready:   return "OnReady";
          case Kind::Enable:  return "OnEnable";
          case Kind::Disable: return "OnDisable";
          case Kind::Custom0: return "OnCustom0";
          case Kind::Custom1: return "OnCustom1";
          case Kind::Custom2: return "OnCustom2";
          case Kind::Custom3: return "OnCustom3";
        }
        return "OnReady";
      }

      // The runtime event-id paired with each Kind. Ready/Enable/Disable
      // mirror P64::EVENT_TYPE_*; custom slots map into the user-event range
      // EVENT_TYPE_CUSTOM_START..EVENT_TYPE_CUSTOM_END so they don't collide.
      static uint16_t kindEventId(Kind k)
      {
        switch (k) {
          case Kind::Ready:   return 0xFFFF - 2; // EVENT_TYPE_READY
          case Kind::Enable:  return 0xFFFF - 0; // EVENT_TYPE_ENABLE
          case Kind::Disable: return 0xFFFF - 1; // EVENT_TYPE_DISABLE
          case Kind::Custom0: return 0x0000;
          case Kind::Custom1: return 0x0001;
          case Kind::Custom2: return 0x0002;
          case Kind::Custom3: return 0x0003;
        }
        return 0xFFFF - 2;
      }

      void updateTitle() {
        setTitle(std::string{ICON_MDI_PLAY " "} + kindLabel(kind));
      }

      PrefabEvent()
      {
        uuid = Utils::Hash::randomU64();
        updateTitle();
        // Red-ish entry node so it stands out from regular nodes; matches
        // the visual weight UE5 gives event nodes in the Event Graph.
        setStyle(std::make_shared<ImFlow::NodeStyle>(IM_COL32(0xCC, 0x44, 0x44, 0xFF), ImColor(0,0,0,255), 4.0f));
        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override
      {
        static const char* labels[] = {
          "OnReady", "OnEnable", "OnDisable",
          "OnCustom0", "OnCustom1", "OnCustom2", "OnCustom3",
        };
        int idx = static_cast<int>(kind);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("##kind", &idx, labels, IM_ARRAYSIZE(labels))) {
          kind = static_cast<Kind>(idx);
          updateTitle();
        }
      }

      void serialize(nlohmann::json &j) override {
        j["kind"] = static_cast<int>(kind);
      }

      void deserialize(nlohmann::json &j) override {
        kind = static_cast<Kind>(j.value("kind", 0));
        updateTitle();
      }

      // Entry nodes don't emit code — codegen routes from the dispatch
      // switch at the top of the function directly to this node's label.
      void build(BuildCtx &ctx) override {
        (void)ctx;
      }
  };
}
