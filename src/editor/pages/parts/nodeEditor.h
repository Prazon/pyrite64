/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/project.h"
#include "../../../project/graph/graph.h"
#include "../../commentFrames.h"
#include "../../nodeFinder.h"
#include "../../graphMinimap.h"
#include "../../marqueeSelect.h"

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
      bool showVarsPanel{true};
      std::string nodeMenuSearch{};
      int nodeMenuFocusFrames{0};

      void drawVariablesPanel();
      void syncVariablePins();
      void drawCreateMenu(ImFlow::Pin* pin);
      void resetView();
      void addGroup();

      // Reveal-from-Compile-Errors state. Same scheme as
      // PrefabEventGraphEditor — see the header there for rationale.
      uint64_t pendingFocusNodeUUID{0};
      uint64_t highlightNodeUUID{0};
      float    highlightSecondsLeft{0.0f};
      bool     forceFocusNextFrame{false};
      bool     firstDockFrame{true};

      // Latched mouse-grid position when the Tab Add-Node palette opens.
      ImVec2   paletteSpawnPos{0,0};

      // Comment-frame paint + drag-containment state.
      Editor::CommentFrames::State commentFrames{};

      // Find-by-title overlay (Ctrl+F).
      Editor::NodeFinder::State finder{};

      // Minimap drag state.
      Editor::GraphMinimap::State minimap{};

      // Marquee box-select state.
      Editor::MarqueeSelect::State marquee{};

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
