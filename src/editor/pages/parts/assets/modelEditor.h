/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"
#include "assetPreviewViewport.h"

namespace Editor
{
  class ModelEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      // SPBF64 fork: 3D preview of the asset rendered above the material UI.
      AssetPreviewViewport preview{};
      void*    previewBoundMesh{nullptr};
      uint64_t previewBoundUUID{0};
      float    previewSplitFrac{0.55f};
      bool     splitDragging{false};
      bool     forceFocusNextFrame{true};
      bool     firstDockFrame{true};

      // Asset inspector right strip.
      float    assetSplitFrac{0.24f};
      bool     assetSplitDragging{false};

    public:
      explicit ModelEditor(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
