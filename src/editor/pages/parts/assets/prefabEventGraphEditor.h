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

    public:
      explicit PrefabEventGraphEditor(uint64_t prefabAssetUUID);

      bool draw();
      void focus() const;
      void save();
      void discardUnsavedChanges();

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;
  };
}
