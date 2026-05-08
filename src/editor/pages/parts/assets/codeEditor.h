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
      std::string winName{};
      std::string filePath{};
      std::unique_ptr<TextEditor> editor{};

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
