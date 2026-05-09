/**
* Per-asset editor for a .p64widget WidgetBlueprint. Mirrors PrefabEditor's
* lifecycle (one instance per open asset, keyed by UUID in
* EditorScene::widgetEditors), but the center pane is a Viewport2D in
* canvas mode instead of a 3D Viewport3D, and the widget tree is restricted
* to 2D-eligible components in the palette.
*
* On disk a .p64widget shares the Prefab serialization shape; a
* WidgetBlueprint is structurally a Prefab whose root Object has
* isCanvas2D=true. The editor rewrites that subtree on save.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

#include "../../../../project/scene/scene.h"
#include "../../../../project/selection.h"
#include "../../../undoRedo.h"
#include "../sceneGraph.h"
#include "../objectInspector.h"
#include "../viewport2D.h"

namespace Editor
{
  class WidgetBlueprintEditor
  {
    private:
      uint64_t assetUUID{};
      std::string filePath{};
      std::string winName{};

      // In-memory scene that holds the widget subtree as root.children[0].
      Project::Scene scene{};
      Project::Selection selection{};

      // Per-editor undo/redo so widget edits don't pollute the main scene's
      // stack.
      Editor::UndoRedo::History history{};

      // Sub-widgets, scoped to this editor's scene + selection.
      SceneGraph graph{};
      ObjectInspector inspector{};

      // 2D canvas preview (locked 320x240 with checkerboard backdrop).
      Viewport2D viewport{scene, selection};

      bool forceFocusNextFrame{true};
      // Last-saved JSON of the widget subtree; used for dirty detection.
      std::string savedJSON{};

    public:
      explicit WidgetBlueprintEditor(uint64_t assetUUID);

      bool draw(ImGuiID defDockId);
      void focus() const;
      void save() { saveToDisk(); }

      [[nodiscard]] uint64_t getAssetUUID() const { return assetUUID; }
      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] std::string getName() const;

    private:
      void loadFromDisk();
      void saveToDisk();
      void drawPalette();
  };
}
