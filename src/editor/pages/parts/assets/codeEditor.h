/**
* In-editor C++ source editor window for project scripts. One instance per
* script asset (keyed by UUID in EditorScene::codeEditors), mirroring the
* ImageEditor / ModelEditor lifecycle. Opens as a floating dockable window
* the user can drag into the main editor or out to its own OS window.
*/
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "imgui.h"
#include "TextEditor.h"

namespace Editor
{
  class CodeEditor
  {
    private:
      uint64_t assetUUID{};
      // When set, the editor was opened by absolute path rather than asset
      // UUID — used for files outside the AssetManager (e.g. per-prefab user
      // sources whose namespace marker doesn't match buildCodeEntry's
      // dispatch). The visible name comes from the path's filename.
      std::string pathOverride{};
      std::string nameOverride{};
      std::string winName{};
      std::string filePath{};
      std::unique_ptr<TextEditor> editor{};

      bool forceFocusNextFrame{true};

      // First-frame dock target override. Beats the loop-passed defDockId so
      // a caller (e.g. PrefabEditor) can drop the new tab next to its own
      // viewport instead of the outer Scene-Editor strip.
      ImGuiID firstDockTarget{0};
      bool   firstDockApplied{false};
      bool   firstDockFrame{true};

      // Last text we wrote / loaded. Used to decide if the buffer is dirty.
      std::string savedText{};

      void loadFromDisk();
      void saveToDisk();
      bool isDirty() const;

    public:
      explicit CodeEditor(uint64_t assetUUID);
      // Path-based ctor: opens an arbitrary file. The instance key (assetUUID)
      // is derived from the absolute path by sha256_64bit so EditorScene's
      // codeEditors map still de-dupes re-opens.
      CodeEditor(uint64_t syntheticUUID, std::string absolutePath);

      bool draw(ImGuiID defDockId);
      void focus() const;
      // Re-arm the first-frame dock override. If the editor was already
      // open and a different host (e.g. another PrefabEditor) re-requests
      // it with a new dock target, we want the next draw to honor that
      // target rather than treating the override as already-consumed.
      void setFirstDockTarget(ImGuiID dockId) {
        firstDockTarget = dockId;
        firstDockApplied = false;
      }
  };
}
