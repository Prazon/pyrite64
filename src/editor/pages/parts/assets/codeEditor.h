/**
* SPBF64 fork: in-editor C++ source editor window for project scripts.
* One instance per script asset (keyed by UUID in EditorScene::codeEditors),
* mirroring the ImageEditor / ModelEditor lifecycle. Auto-docks as a tab
* next to the 3D-Viewport on first appearance.
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
      std::string winName{};
      std::string filePath{};
      std::unique_ptr<TextEditor> editor{};

      // Set on construction; consumed once to dock next to the scene viewport.
      bool dockOnFirstAppearance{true};
      bool forceFocusNextFrame{true};

      // Last text we wrote / loaded. Used to decide if the buffer is dirty.
      std::string savedText{};

      void loadFromDisk();
      void saveToDisk();
      bool isDirty() const;

    public:
      explicit CodeEditor(uint64_t assetUUID);

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
