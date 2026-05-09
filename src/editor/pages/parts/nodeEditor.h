/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/project.h"
#include "../../../project/graph/graph.h"

namespace Editor
{
  class NodeEditor
  {
    private:
      Project::AssetManagerEntry *currentAsset{nullptr};
      Project::Graph::Graph graph{};
      std::string name{};
      std::string savedState{};
      std::string trackedDirtyState{};
      bool dirty{false};
      bool isInit{false};

      // Reveal-from-Compile-Errors state. Same scheme as
      // PrefabEventGraphEditor — see the header there for rationale.
      uint64_t pendingFocusNodeUUID{0};
      uint64_t highlightNodeUUID{0};
      float    highlightSecondsLeft{0.0f};
      bool     forceFocusNextFrame{false};
      bool     firstDockFrame{true};

    public:
      NodeEditor(uint64_t assetUUID);
      ~NodeEditor();
      bool draw(ImGuiID defDockId);
      void save();
      void discardUnsavedChanges();
      void focus() const;

      // Queue a node-focus request consumed by the next draw frame. Mirrors
      // PrefabEventGraphEditor::requestFocusNode; behavior is identical.
      void requestFocusNode(uint64_t nodeUUID) {
        pendingFocusNodeUUID = nodeUUID;
        forceFocusNextFrame  = true;
      }

      [[nodiscard]] bool isDirty() const { return dirty; }
      [[nodiscard]] uint64_t getAssetUUID() const { return currentAsset ? currentAsset->getUUID() : 0; }
      [[nodiscard]] const std::string &getName() const { return name; }
  };
}
