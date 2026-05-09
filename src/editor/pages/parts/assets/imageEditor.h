/**
* SPBF64 fork: dockable per-asset image preview window.
* Mirrors the ModelEditor lifecycle: held in EditorScene's imageEditors map,
* persisted across sessions via editorScene.json.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class ImageEditor
  {
    public:
      // View state — persisted only across the lifetime of this window
      enum class ZoomMode { Fit, X1, X2, X4, X8, X16 };

    private:
      uint64_t assetUUID{};
      std::string winName{};

      ZoomMode zoomMode{ZoomMode::Fit};
      ImVec2   panOffset{0, 0};
      bool     showChecker{true};

      // Force focus on first appearance so the window doesn't open silently
      // behind another tab in a saved dock node.
      bool   forceFocusNextFrame{true};
      bool   firstDockFrame{true};

      // Asset inspector right strip: width fraction, drag flag.
      float  assetSplitFrac{0.30f};
      bool   assetSplitDragging{false};

      // Sprite slicing overlay state — purely visualization in v1, not persisted
      bool   sliceShow{false};
      int    sliceCellW{16};
      int    sliceCellH{16};
      int    slicePivotX{8};
      int    slicePivotY{16};
      int    sliceFrame{0};

      void drawToolbar(int imgW, int imgH);
      void drawCanvas(ImTextureID tex, int imgW, int imgH);
      void drawSlicePanel(int imgW, int imgH);

    public:
      explicit ImageEditor(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
