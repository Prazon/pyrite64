/**
* Per-asset window for RESOURCE_INSTANCE assets. Hosts the per-field
* instance editor on the left (uuid-keyed for editor-authored types,
* string-keyed for header-authored ones) and the AssetInspector strip
* on the right.
*/
#pragma once
#include <cstdint>
#include <string>

#include "imgui.h"

namespace Editor
{
  class ResourceInstanceEditor
  {
    private:
      uint64_t assetUUID{};
      std::string winName{};

      float splitFrac{0.32f};
      bool  splitDragging{false};

      bool forceFocusNextFrame{true};
      bool firstDockFrame{true};

      void drawInstancePane();

    public:
      explicit ResourceInstanceEditor(uint64_t assetUUID) : assetUUID(assetUUID) {}

      bool draw(ImGuiID defDockId);
      void focus() const;
  };
}
