/**
* Per-asset window for RESOURCE_TYPE assets. Mirrors ImageEditor lifecycle:
* held in EditorScene's resourceTypeEditors map. The main pane hosts the
* existing schema editor (Editor::ResourceTypeEditor::draw); the right
* strip hosts AssetInspector for File header / per-asset settings.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class ResourceTypeEditorWindow
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      float splitFrac{0.28f};
      bool  splitDragging{false};

      bool forceFocusNextFrame{true};
      bool firstDockFrame{true};

    public:
      explicit ResourceTypeEditorWindow(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
