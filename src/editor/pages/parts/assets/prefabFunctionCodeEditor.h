// Per-function code editor: shows the source of a single P64_NODE function
// from <project>/src/user/<prefab>.cpp instead of the whole file. On save
// the slice is spliced back into the .cpp and the matching declaration in
// <prefab>.h is rewritten if the user changed the signature.
//
// Sibling of CodeEditor (which handles whole-file scripts and stays
// untouched). Keyed by (prefabName, functionName) via a synthetic UUID so
// the editorScene can de-dupe re-opens and tear instances down when the
// parent PrefabEditor closes.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "imgui.h"
#include "TextEditor.h"

namespace Editor
{
  class PrefabFunctionCodeEditor
  {
    private:
      uint64_t syntheticUUID{};
      std::string prefabName{};
      // Mutable: if the user edits the signature, save() detects the new
      // name and we track it forward so subsequent saves still find the
      // right slice.
      std::string funcName{};

      std::string winName{};
      std::unique_ptr<TextEditor> editor{};
      bool forceFocusNextFrame{true};

      // First-frame dock target — same belt-and-braces approach as
      // CodeEditor (DockBuilderDockWindow + SetNextWindowDockID(Always)).
      ImGuiID firstDockTarget{0};
      bool   firstDockApplied{false};
      bool   firstDockFrame{true};

      // Snapshot of the slice last loaded/saved — drives the dirty marker.
      std::string savedText{};

      void loadFromDisk();
      void saveToDisk();
      bool isDirty() const;

    public:
      PrefabFunctionCodeEditor(
        uint64_t syntheticUUID,
        std::string prefabName,
        std::string funcName
      );

      bool draw(ImGuiID defDockId);
      void focus() const;
      void setFirstDockTarget(ImGuiID dockId) {
        firstDockTarget = dockId;
        firstDockApplied = false;
      }

      [[nodiscard]] uint64_t getSyntheticUUID() const { return syntheticUUID; }
      [[nodiscard]] const std::string& getPrefabName() const { return prefabName; }
      [[nodiscard]] const std::string& getFunctionName() const { return funcName; }
  };
}
