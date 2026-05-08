// added by SPBF64 fork
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include "../../../../project/scene/scene.h"
#include "../../../../project/selection.h"
#include "../../../undoRedo.h"
#include "../sceneGraph.h"
#include "../objectInspector.h"
#include "../viewport3D.h"

namespace Editor
{
  /**
   * Per-asset editor for a Project::Prefab. Mirrors the CodeEditor /
   * NodeEditor lifecycle (one instance per open prefab, keyed by asset UUID
   * in EditorScene::prefabEditors). Hosts the prefab's Object subtree as the
   * single child of an in-memory Project::Scene, then drives Editor::SceneGraph
   * and Editor::ObjectInspector against that scene + a private Selection.
   *
   * Each instance owns its own UndoRedo::History so prefab edits don't share
   * the main scene's undo stack. Saves write the subtree back to the .prefab
   * file at the asset's path.
   */
  class PrefabEditor
  {
    private:
      uint64_t assetUUID{};
      std::string filePath{};
      std::string winName{};

      // In-memory scene that holds the prefab subtree as root.children[0].
      Project::Scene scene{};
      Project::Selection selection{};

      // Per-editor undo/redo so this prefab's edits don't pollute the main
      // scene's stack.
      Editor::UndoRedo::History history{};

      // Sub-widgets, scoped to this editor's scene + selection.
      SceneGraph graph{};
      ObjectInspector inspector{};

      // 3D preview of the prefab — same Viewport3D class that drives the main
      // editor's 3D viewport, but bound to this editor's in-memory scene +
      // selection so picking, gizmos, drag-drop, and component highlights all
      // operate on the prefab subtree.
      Viewport3D viewport{scene, selection};

      bool dockOnFirstAppearance{true};
      bool forceFocusNextFrame{true};
      // Last-saved JSON of the prefab subtree; used for dirty detection.
      std::string savedJSON{};
      // Splitter fractions for the 3-pane body: hierarchy | viewport | inspector.
      float leftSplitFrac{0.22f};
      float rightSplitFrac{0.30f};

      void loadFromDisk();
      void saveToDisk();

    public:
      explicit PrefabEditor(uint64_t assetUUID);

      bool draw(ImGuiID defDockId);
      void focus() const;
      // Public save: triggered by the unsaved-on-close modal in EditorScene.
      void save() { saveToDisk(); }

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;
  };
}
