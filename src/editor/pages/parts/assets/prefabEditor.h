// added by SPBF64 fork
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include <vector>

#include "../../../../project/scene/scene.h"
#include "../../../../project/scene/prefab.h"
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
      // Last-saved serialized form of the variables list, for dirty tracking
      // independent of the obj subtree.
      std::string savedVarsJSON{};
      // Local working copy of the prefab class variables. Synced from
      // asset->prefab on load, written back on save. Editing this list goes
      // through the editor's history.
      std::vector<Project::PrefabVarDef> variables{};
      // Splitter fractions for the Components tab's 3-pane body.
      float leftSplitFrac{0.22f};
      float rightSplitFrac{0.30f};

      enum class Tab : int { Components = 0, Variables = 1 };
      Tab activeTab{Tab::Components};

      void loadFromDisk();
      void saveToDisk();
      void drawComponentsTab();
      void drawVariablesTab();

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
