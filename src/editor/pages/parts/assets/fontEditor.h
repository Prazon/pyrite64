/**
* Per-asset window for FONT assets. Mirrors ImageEditor lifecycle:
* held in EditorScene's fontEditors map, opened by double-clicking a
* font in the asset browser. Hosts the AssetInspector right strip so
* font settings (size, charset, ID) are editable per-asset.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class FontEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      // Right asset-inspector strip: width as fraction of total inner width.
      float splitFrac{0.32f};
      bool  splitDragging{false};

      bool forceFocusNextFrame{true};
      bool firstDockFrame{true};

    public:
      explicit FontEditor(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
