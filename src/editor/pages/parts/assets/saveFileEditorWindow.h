/**
* Per-asset window for SAVE_FILE assets. Mirrors ResourceTypeEditorWindow:
* schema editor on the left, AssetInspector on the right.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class SaveFileEditorWindow
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      float splitFrac{0.28f};
      bool  splitDragging{false};

      bool forceFocusNextFrame{true};
      bool firstDockFrame{true};

    public:
      explicit SaveFileEditorWindow(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
