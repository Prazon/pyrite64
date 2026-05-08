#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include "../../../../project/graph/graph.h"

namespace Editor
{
  /**
   * Floating window that hosts a prefab's default Event Graph. Lifecycle
   * mirrors PrefabEditor / NodeEditor — one instance per open prefab graph,
   * keyed by prefab asset UUID in EditorScene::prefabEventGraphEditors.
   *
   * Storage lives on Project::Prefab::eventGraphJSON. The editor materializes
   * a live Project::Graph::Graph from that string on open and serializes
   * back to the prefab on save.
   */
  class PrefabEventGraphEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};
      Project::Graph::Graph graph{};
      std::string savedState{};
      bool isInit{false};
      bool forceFocusNextFrame{true};

      // Caller-supplied first-frame dock override (PrefabEditor uses this so
      // the EventGraph window opens as a tab next to its viewport instead of
      // landing on the outer Scene-Editor strip).
      ImGuiID firstDockTarget{0};
      bool   firstDockApplied{false};

    public:
      explicit PrefabEventGraphEditor(uint64_t prefabAssetUUID);

      bool draw(ImGuiID defDockId = 0);
      void focus() const;
      void save();
      void discardUnsavedChanges();
      // Re-arm the first-frame dock override on a re-open call so a new
      // host can land the window where it wants even if the editor was
      // already alive. See codeEditor.h for matching rationale.
      void setFirstDockTarget(ImGuiID dockId) {
        firstDockTarget = dockId;
        firstDockApplied = false;
      }

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;
  };
}
