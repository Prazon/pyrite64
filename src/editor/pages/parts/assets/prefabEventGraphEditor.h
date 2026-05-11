#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include "../../../../project/graph/graph.h"
#include "../../../commentFrames.h"
#include "../../../nodeFinder.h"
#include "../../../graphMinimap.h"

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
      bool   firstDockFrame{true};

      // Reveal-from-Compile-Errors state. Set by requestFocusNode(); the next
      // draw frame consumes pendingFocusNodeUUID — it pans the canvas to
      // center the node and arms a brief overlay rect that decays over a few
      // hundred ms so the user can see *which* node was opened.
      uint64_t pendingFocusNodeUUID{0};
      uint64_t highlightNodeUUID{0};
      float    highlightSecondsLeft{0.0f};

      // Latched mouse-grid position when Tab opens the Add-Node palette.
      ImVec2   paletteSpawnPos{0,0};

      // Comment-frame paint + drag-containment state.
      Editor::CommentFrames::State commentFrames{};

      // Find-by-title overlay (Ctrl+F).
      Editor::NodeFinder::State finder{};

      // Minimap drag state.
      Editor::GraphMinimap::State minimap{};

    public:
      explicit PrefabEventGraphEditor(uint64_t prefabAssetUUID);

      bool draw(ImGuiID defDockId = 0);
      void focus() const;
      void save();
      void discardUnsavedChanges();

      // Queue a node-focus request. The next draw frame pans the canvas to
      // center the node (using ImNodeFlow's setScroll) and starts a brief
      // colored-outline overlay so the user can spot the node visually.
      void requestFocusNode(uint64_t nodeUUID) {
        pendingFocusNodeUUID = nodeUUID;
        forceFocusNextFrame  = true;
      }
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
