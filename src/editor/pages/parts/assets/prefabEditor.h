// added by SPBF64 fork
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include <vector>

#include "../../../../project/scene/scene.h"
#include "../../../../project/scene/prefab.h"
#include "../../../../project/selection.h"
#include "../../../../project/prefabFunctions.h"
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
      // Splitter fractions for the 3-pane body.
      float leftSplitFrac{0.22f};
      float rightSplitFrac{0.30f};
      // Vertical split inside the left pane: top is the components tree,
      // bottom is the "My Prefab" panel (variables + functions).
      float leftVerticalSplitFrac{0.55f};

      // Dock node ID of this editor's viewport tab — captured at firstBuild
      // and refreshed each frame from the live ImGui window so it survives
      // user rearrangements. Passed to openPrefabEventGraphEditor /
      // openCodeEditorByPath so the EventGraph and function-source tabs land
      // as siblings of the viewport instead of on the outer Scene-Editor strip.
      ImGuiID viewportDockNodeID{0};

      // Synthetic CodeEditor UUIDs we've spawned for this prefab's
      // function bodies. Used at close time so the parent EditorScene can
      // tear down those tabs when the prefab editor itself is dismissed —
      // otherwise undocked function tabs linger as ghost windows referring
      // to a no-longer-open prefab.
      std::vector<uint64_t> ownedCodeEditorUUIDs{};

      // Cached list of P64_NODE-tagged user functions for this prefab.
      // Refreshed each frame the Functions section is visible — file scans
      // are cheap and the cost-of-stale is otherwise immediate confusion.
      std::vector<Project::PrefabFunctionDesc> functions{};

      // What the right-side details panel is showing. Object is the default
      // (the existing ObjectInspector view). Selecting a variable or function
      // in the My Prefab panel switches the details to a UE-Blueprint-style
      // editor for that entry; clearing or selecting an Object node switches
      // back. Index is into `variables` for VARIABLE; for FUNCTION we store
      // the function name (the scan list is rebuilt each frame, so name is
      // the stable identifier we can reselect against).
      enum class DetailsKind : int { OBJECT = 0, VARIABLE = 1, FUNCTION = 2, COMPONENT = 3 };
      DetailsKind detailsKind{DetailsKind::OBJECT};
      int          detailsVarIdx{-1};
      std::string  detailsFuncName{};
      // Selected component within the focused object; non-zero means show
      // ONLY that component in the details panel (UE-Blueprint behavior:
      // clicking a component leaf in the hierarchy filters details).
      uint64_t     detailsCompUUID{0};
      uint32_t     detailsCompObjUUID{0};
      // Rename buffer used by the function-details rename input — separate
      // so the user can stage a name before committing the file rewrite.
      std::string  renameBuffer{};
      // Tracks the SceneGraph's current primary selection so we can switch
      // the details panel back to the Object inspector the moment the user
      // clicks an object node (UE-Blueprint behavior: object & my-prefab
      // selections are mutually exclusive in the details pane).
      uint32_t     lastSelectionPrimary{0};

      void loadFromDisk();
      void saveToDisk();
      void drawLeftPane();
      void drawGraphsPanel();
      void drawVariablesPanel();
      void drawFunctionsPanel();
      void drawDetailsPanel();
      void drawVariableDetails();
      void drawFunctionDetails();
      void drawComponentDetails();
      // Clear the variable/function selection so the details panel falls
      // back to the object inspector. Called when the user picks an Object
      // node (since they're navigating the hierarchy now).
      void clearMyPrefabSelection();

    public:
      explicit PrefabEditor(uint64_t assetUUID);

      bool draw(ImGuiID defDockId);
      void focus() const;
      // Public save: triggered by the unsaved-on-close modal in EditorScene.
      void save() { saveToDisk(); }

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;
      [[nodiscard]] const std::vector<uint64_t>& getOwnedCodeEditorUUIDs() const {
        return ownedCodeEditorUUIDs;
      }
  };
}
